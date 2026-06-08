//
// Created by Nevermore on 2025/3/6.
//

#include "NativeHardwareDecoder.h"

#include "AVFrame.hpp"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNISignature.h"
#include "NativeDecoderManager.h"

using namespace slark;
using namespace slark::JNI;
constexpr std::string_view kMediaCodecDecoderClass = "com/slark/sdk/MediaCodecDecoder";

Error Native_HardwareDecoder_sendPacket(JNIEnv* env,
                                        std::string_view decoderId,
                                        DataPtr& data,
                                        int64_t pts,
                                        NativeDecodeFlag flag,
                                        PacketType type) {
    if ((data == nullptr || data->empty()) && flag != NativeDecodeFlag::EndOfStream) {
        LogE("data error");
        return Error::CodecInputError;
    }
    auto decoderClass = JNICache::instance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return Error::CodecError;
    }
    auto methodSignature = JNI::makeSignature(JNI::Sig::Int,
                                              JNI::Sig::String,
                                              JNI::makeArray(JNI::Sig::Byte),
                                              JNI::Sig::Long,
                                              JNI::Sig::Int);
    auto methodId =
        JNICache::instance().getStaticMethod(decoderClass, "sendPacket", methodSignature);
    if (!methodId) {
        LogE("create decoder failed, not found send data method");
        return Error::CodecError;
    }
    JNIReference<jbyteArray> byteArray(env, nullptr);
    if (flag != NativeDecodeFlag::EndOfStream) {
        if (type == PacketType::Video) {
            byteArray = ToJVM::naluBytes(env, *data, /*skipExistingHeader=*/true);
        } else {
            byteArray = ToJVM::bytes(env, *data);
        }
    } else {
        // EndOfStream 时 byteArray 保持 null，Kotlin 侧 flag 检查在前，不会访问 byteArray
    }

    int res = JNI::JNICall::callStatic<int32_t>(
                  env,
                  kMediaCodecDecoderClass,
                  "sendPacket",
                  JNI::JObjectWithSig(ToJVM::str(env, decoderId), JNI::Sig::String),
                  JNI::JObjectWithSig(std::move(byteArray), JNI::Sig::ByteArray),
                  static_cast<int64_t>(pts),
                  static_cast<int32_t>(flag))
                  .value_or(static_cast<int32_t>(Error::CodecError));
    return static_cast<Error>(res);
}

// 构造 Kotlin VideoDecoderConfig 对象
static ObjectReference buildVideoDecoderConfig(JNIEnv* env,
                                               const std::shared_ptr<VideoDecoderConfig>& cfg,
                                               DecodeMode mode) {
    constexpr std::string_view kConfigClass = "com/slark/sdk/VideoDecoderConfig";
    constexpr std::string_view kDecodeModeClass = "com/slark/sdk/DecodeMode";
    const char* modeFieldName = (mode == DecodeMode::Texture) ? "Texture" : "ByteBuffer";
    auto modeEnum = JNICache::instance().getEnumField(env, kDecodeModeClass, modeFieldName);
    if (!modeEnum) {
        LogE("not found DecodeMode enum field: {}", modeFieldName);
        return {env, nullptr};
    }
    return JNI::JNICall::create(
        env,
        kConfigClass,
        static_cast<int32_t>(cfg->width),
        static_cast<int32_t>(cfg->height),
        static_cast<int32_t>(cfg->profile),
        static_cast<int32_t>(cfg->level),
        JNI::JObjectWithSig(std::move(modeEnum), JNI::makeObject(kDecodeModeClass)));
}

std::string Native_HardwareDecoder_createVideoDecoder(
    JNIEnv* env,
    const std::shared_ptr<VideoDecoderConfig>& decoderConfig,
    DecodeMode mode) {
    if (!decoderConfig) {
        LogE("config is nullptr");
        return {};
    }
    constexpr std::string_view kConfigClass = "com/slark/sdk/VideoDecoderConfig";
    auto configObj = buildVideoDecoderConfig(env, decoderConfig, mode);
    if (!configObj) {
        LogE("build VideoDecoderConfig failed");
        return {};
    }

    // 构建 CSD 数据（Annex B: start code + NAL），通过 MediaFormat csd-0 传入，不占输入 slot
    auto decoderInfo = std::make_unique<Data>();
    static constexpr uint8_t kNaluStartCode[] = {0x00, 0x00, 0x00, 0x01};
    Data naluHeader(4, kNaluStartCode);
    if (decoderConfig->vps) {
        decoderInfo->append(naluHeader);
        decoderInfo->append(*decoderConfig->vps);
    }
    decoderInfo->append(naluHeader);
    decoderInfo->append(*decoderConfig->sps);
    decoderInfo->append(naluHeader);
    decoderInfo->append(*decoderConfig->pps);
    auto csdBytes = ToJVM::naluBytes(env, *decoderInfo, /*skipExistingHeader=*/true);

    auto jDecoderId = JNI::JNICall::callStatic<jobject>(
        env,
        kMediaCodecDecoderClass,
        "createVideoDecoder",
        JNI::ReturnSig{JNI::Sig::String},
        JNI::JObjectWithSig(ToJVM::str(env, decoderConfig->mediaInfo), JNI::Sig::String),
        JNI::JObjectWithSig(std::move(configObj), JNI::makeObject(kConfigClass)),
        JNI::JObjectWithSig(std::move(csdBytes), JNI::Sig::ByteArray));
    auto decoderId =
        jDecoderId ? FromJVM::str(env, reinterpret_cast<jstring>(jDecoderId.get())) : std::string{};
    if (decoderId.empty()) {
        LogE("create video decoder failed, decoderId is empty");
        return {};
    }
    LogI("create video decoder:{}", decoderId);
    return decoderId;
}

static ObjectReference buildAudioDecoderConfig(JNIEnv* env,
                                               const std::shared_ptr<AudioDecoderConfig>& cfg) {
    constexpr std::string_view kConfigClass = "com/slark/sdk/AudioDecoderConfig";
    return JNI::JNICall::create(env,
                                kConfigClass,
                                static_cast<int32_t>(cfg->sampleRate),
                                static_cast<int32_t>(cfg->channels),
                                static_cast<int32_t>(cfg->profile),
                                static_cast<int32_t>(cfg->samplingFrequencyIndex),
                                static_cast<int32_t>(cfg->audioObjectTypeExt),
                                static_cast<int32_t>(cfg->samplingFreqIndexExt));
}

std::string Native_HardwareDecoder_createAudioDecoder(
    JNIEnv* env,
    const std::shared_ptr<AudioDecoderConfig>& decoderConfig) {
    constexpr std::string_view kConfigClass = "com/slark/sdk/AudioDecoderConfig";
    auto configObj = buildAudioDecoderConfig(env, decoderConfig);
    if (!configObj) {
        LogE("build AudioDecoderConfig failed");
        return {};
    }
    // 使用 ReturnSig 指定返回类型为 String，而非推导为 jobject(Ljava/lang/Object;)
    auto jDecoderId = JNI::JNICall::callStatic<jobject>(
        env,
        kMediaCodecDecoderClass,
        "createAudioDecoder",
        JNI::ReturnSig{JNI::Sig::String},
        JNI::JObjectWithSig(ToJVM::str(env, decoderConfig->mediaInfo), JNI::Sig::String),
        JNI::JObjectWithSig(std::move(configObj), JNI::makeObject(kConfigClass)));
    return jDecoderId ? FromJVM::str(env, reinterpret_cast<jstring>(jDecoderId.get()))
                      : std::string{};
}

void Native_HardwareDecoder_releaseDecoder(JNIEnv* env, std::string_view decoderId) {
    JNI::JNICall::callStatic<void>(env,
                                   kMediaCodecDecoderClass,
                                   "releaseDecoder",
                                   std::string(decoderId));
}

void Native_HardwareDecoder_flush(JNIEnv* env, std::string_view decoderId) {
    constexpr std::string_view kActionClass = "com/slark/sdk/MediaCodecDecoder$Action";
    auto enumValue = JNICache::instance().getEnumField(env, kActionClass, "Flush");
    if (!enumValue) {
        LogE("not found Flush enum field");
        return;
    }
    JNI::JNICall::callStatic<void>(
        env,
        kMediaCodecDecoderClass,
        "doAction",
        JNI::JObjectWithSig(ToJVM::str(env, decoderId), JNI::Sig::String),
        JNI::JObjectWithSig(std::move(enumValue), JNI::makeObject(kActionClass)));
}

// only for audio decoder
extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_MediaCodecDecoder_processRawData(JNIEnv* env,
                                                    jobject /* thiz */,
                                                    jstring jDecoderId,
                                                    jbyteArray byteBuffer,
                                                    jlong pts,
                                                    jboolean isCompleted) {
    using namespace slark;
    LogI("native decode success:{}", pts);
    auto decoderId = FromJVM::str(env, jDecoderId);
    // send to manager
    auto decoder = NativeDecoderManager::shareInstance().find(decoderId);
    if (!decoder) {
        LogE("not found decoder, decoderId:{}", decoderId);
        return;
    }
    auto dataPtr = FromJVM::bytes(env, byteBuffer);
    decoder->receiveDecodedData(std::move(dataPtr), pts, (JNI_TRUE == isCompleted));
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_MediaCodecDecoder_processCodecError(JNIEnv* env,
                                                       jobject /* thiz */,
                                                       jstring jDecoderId,
                                                       jint errorCode) {
    using namespace slark;
    auto decoderId = FromJVM::str(env, jDecoderId);
    auto decoder = NativeDecoderManager::shareInstance().find(decoderId);
    if (!decoder) {
        LogE("processCodecError: not found decoder:{}", decoderId);
        return;
    }
    LogE("codec error, decoderId:{} errorCode:{}", decoderId, errorCode);
    decoder->onError(Error::CodecError);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_MediaCodecDecoder_onVideoSizeChanged(JNIEnv* env,
                                                        jobject /* thiz */,
                                                        jstring jDecoderId,
                                                        jint width,
                                                        jint height) {
    using namespace slark;
    auto decoderId = FromJVM::str(env, jDecoderId);
    auto decoder = NativeDecoderManager::shareInstance().find(decoderId);
    if (!decoder) {
        LogE("onVideoSizeChanged: not found decoder:{}", decoderId);
        return;
    }
    decoder->onVideoSizeChanged(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

uint64_t Native_HardwareDecoder_requestVideoFrame(JNIEnv* env,
                                                  std::string_view decoderId,
                                                  uint64_t waitTime,
                                                  uint32_t width,
                                                  uint32_t height) {
    auto packed = JNI::JNICall::callStatic<int64_t>(env,
                                                    kMediaCodecDecoderClass,
                                                    "requestVideoFrame",
                                                    std::string(decoderId),
                                                    static_cast<int64_t>(waitTime),
                                                    static_cast<int32_t>(width),
                                                    static_cast<int32_t>(height))
                      .value_or(0);
    return static_cast<uint64_t>(packed);
}

namespace slark {

std::string NativeHardwareDecoder::createVideoDecoder(
    const std::shared_ptr<VideoDecoderConfig>& decoderConfig,
    DecodeMode mode) {
    if (!decoderConfig) {
        return {};
    }
    if (decoderConfig->sps == nullptr || decoderConfig->pps == nullptr) {
        LogE("sps or pps is nullptr");
        return {};
    }
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_createVideoDecoder(envGuard.get(), decoderConfig, mode);
}

std::string NativeHardwareDecoder::createAudioDecoder(
    const std::shared_ptr<AudioDecoderConfig>& decoderConfig) {
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_createAudioDecoder(envGuard.get(), decoderConfig);
}

Error NativeHardwareDecoder::sendPacket(std::string_view decoderId,
                                        DataPtr& data,
                                        int64_t pts,
                                        NativeDecodeFlag flag,
                                        PacketType type) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return Error::CodecNotFound;
    }
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_sendPacket(envGuard.get(), decoderId, data, pts, flag, type);
}

void NativeHardwareDecoder::release(std::string_view decoderId) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return;
    }
    JNIEnvGuard envGuard(getJavaVM());
    Native_HardwareDecoder_releaseDecoder(envGuard.get(), decoderId);
}

void NativeHardwareDecoder::flush(std::string_view decoderId) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return;
    }
    JNIEnvGuard envGuard(getJavaVM());
    Native_HardwareDecoder_flush(envGuard.get(), decoderId);
}

VideoFrameResult NativeHardwareDecoder::requestVideoFrame(std::string_view decoderId,
                                                          uint64_t waitTime,
                                                          uint32_t width,
                                                          uint32_t height) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return {};
    }
    JNIEnvGuard envGuard(getJavaVM());
    auto packed = Native_HardwareDecoder_requestVideoFrame(envGuard.get(),
                                                           decoderId,
                                                           waitTime,
                                                           width,
                                                           height);
    return VideoFrameResult::unpack(packed);
}

} // namespace slark
