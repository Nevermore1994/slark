//
// Created by Nevermore on 2025/3/6.
//

#include "NativeHardwareDecoder.h"

#include "AVFrame.hpp"
#include "JNICache.h"
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
                                        bool isVideo) {
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
        if (isVideo) {
            // For video, need to prepend NALU header, Annex b format
            byteArray = ToJVM::naluBytes(env, *data);
        } else {
            // For audio, use the raw data directly
            byteArray = ToJVM::bytes(env, *data);
        }
    } else {
        byteArray = ToJVM::bytes(env, Data());
    }

    auto jDecodeId = ToJVM::str(env, decoderId);
    int res = env->CallStaticIntMethod(decoderClass.get(),
                                       methodId.get(),
                                       jDecodeId.detach(),
                                       byteArray.detach(),
                                       static_cast<jlong>(pts),
                                       static_cast<jint>(flag));
    return static_cast<Error>(res);
}

std::string Native_HardwareDecoder_createVideoDecoder(
    JNIEnv* env,
    const std::shared_ptr<VideoDecoderConfig>& decoderConfig,
    DecodeMode mode) {
    if (!decoderConfig) {
        LogE("config is nullptr");
        return {};
    }
    auto decoderClass = JNICache::instance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return {};
    }
    auto methodSignature =
        JNI::makeSignature(JNI::Sig::String, JNI::Sig::String, JNI::makeArray(JNI::Sig::Int));
    auto createDecoderMethod =
        JNICache::instance().getStaticMethod(decoderClass, "createVideoDecoder", methodSignature);
    if (!createDecoderMethod) {
        LogE("create decoder failed, not found decoder method");
        return {};
    }
    auto videoInfo = ToJVM::ints(env,
                                 {
                                     static_cast<int>(decoderConfig->width),
                                     static_cast<int>(decoderConfig->height),
                                     static_cast<int>(decoderConfig->profile),
                                     static_cast<int>(decoderConfig->level),
                                     static_cast<int>(mode),
                                 });
    auto jMediaInfo = ToJVM::str(env, decoderConfig->mediaInfo);
    auto jDecoderId =
        reinterpret_cast<jstring>(env->CallStaticObjectMethod(decoderClass.get(),
                                                              createDecoderMethod.get(),
                                                              jMediaInfo.detach(),
                                                              videoInfo.detach()));

    auto decoderId = FromJVM::str(env, jDecoderId);
    if (decoderId.empty()) {
        LogE("create video decoder failed, decoderId is empty");
        return {};
    } else {
        LogI("create video decoder:{}", decoderId);
    }

    auto decoderInfo = std::make_unique<Data>();
    Data naluHeader(4);
    naluHeader.rawData[0] = 0x00;
    naluHeader.rawData[1] = 0x00;
    naluHeader.rawData[2] = 0x00;
    naluHeader.rawData[3] = 0x01;
    naluHeader.length = 4;
    decoderInfo->append(naluHeader);
    decoderInfo->append(*decoderConfig->sps);
    decoderInfo->append(naluHeader);
    decoderInfo->append(*decoderConfig->pps);
    if (decoderConfig->vps) {
        decoderInfo->append(naluHeader);
        decoderInfo->append(*decoderConfig->vps);
    }
    Native_HardwareDecoder_sendPacket(env,
                                      decoderId,
                                      decoderInfo,
                                      0,
                                      NativeDecodeFlag::CodecConfig,
                                      true);
    return decoderId;
}

std::string Native_HardwareDecoder_createAudioDecoder(
    JNIEnv* env,
    const std::shared_ptr<AudioDecoderConfig>& decoderConfig) {
    auto decoderClass = JNICache::instance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found audio decoder class");
        return "";
    }
    auto methodSignature =
        JNI::makeSignature(JNI::Sig::String, JNI::Sig::String, JNI::makeArray(JNI::Sig::Int));
    auto createDecoderMethod =
        JNICache::instance().getStaticMethod(decoderClass, "createAudioDecoder", methodSignature);
    if (!createDecoderMethod) {
        LogE("create decoder failed, not found decoder method");
        return "";
    }
    auto jMediaInfo = ToJVM::str(env, decoderConfig->mediaInfo);
    auto extraInfo = ToJVM::ints(env,
                                 {static_cast<int>(decoderConfig->sampleRate),
                                  static_cast<int>(decoderConfig->channels),
                                  static_cast<int>(decoderConfig->profile),
                                  static_cast<int>(decoderConfig->samplingFrequencyIndex),
                                  static_cast<int>(decoderConfig->audioObjectTypeExt),
                                  static_cast<int>(decoderConfig->samplingFreqIndexExt)});
    auto jDecoderId =
        reinterpret_cast<jstring>(env->CallStaticObjectMethod(decoderClass.get(),
                                                              createDecoderMethod.get(),
                                                              jMediaInfo.detach(),
                                                              extraInfo.detach()));
    auto decoderId = FromJVM::str(env, jDecoderId);
    return decoderId;
}

void Native_HardwareDecoder_releaseDecoder(JNIEnv* env, std::string_view decoderId) {
    auto decoderClass = JNICache::instance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }
    auto methodSignature = JNI::makeSignature(JNI::Sig::Void, JNI::Sig::String);
    auto methodId =
        JNICache::instance().getStaticMethod(decoderClass, "releaseDecoder", methodSignature);
    if (!methodId) {
        LogE("create decoder failed, not found decoder method");
        return;
    }
    jstring jDecodeId = env->NewStringUTF(decoderId.data());
    env->CallStaticVoidMethod(decoderClass.get(), methodId.get(), jDecodeId);
}

void Native_HardwareDecoder_flush(JNIEnv* env, std::string_view decoderId) {
    constexpr std::string_view kActionClass = "com/slark/sdk/MediaCodecDecoder$Action";
    auto decoderClass = JNICache::instance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }
    jstring jDecoderId = env->NewStringUTF(decoderId.data());
    auto enumValue = JNICache::instance().getEnumField(env, kActionClass, "Flush");
    auto methodSignature =
        JNI::makeSignature(JNI::Sig::Void, JNI::Sig::String, JNI::makeObject(kActionClass));
    auto methodId = JNICache::instance().getStaticMethod(decoderClass, "doAction", methodSignature);
    env->CallStaticVoidMethod(decoderClass.get(), methodId.get(), jDecoderId, enumValue.get());
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

/// \brief Request a video frame from the hardware decoder
uint64_t Native_HardwareDecoder_requestVideoFrame(JNIEnv* env,
                                                  std::string_view decoderId,
                                                  uint64_t waitTime,
                                                  uint32_t width,
                                                  uint32_t height) {
    auto decoderClass = JNICache::instance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("not found video decoder class");
        return 0;
    }
    auto jDecoderId = ToJVM::str(env, decoderId);
    auto methodSignature = JNI::makeSignature(JNI::Sig::Long,
                                              JNI::Sig::String,
                                              JNI::Sig::Long,
                                              JNI::Sig::Int,
                                              JNI::Sig::Int);
    auto methodId =
        JNICache::instance().getStaticMethod(decoderClass, "requestVideoFrame", methodSignature);
    if (!methodId) {
        LogE("not found requestVideoFrame method");
        return 0;
    }
    auto packed = env->CallStaticLongMethod(decoderClass.get(),
                                            methodId.get(),
                                            jDecoderId.detach(),
                                            static_cast<jlong>(waitTime),
                                            static_cast<jint>(width),
                                            static_cast<jint>(height));
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
                                        bool isVideo) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return Error::CodecNotFound;
    }
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_sendPacket(envGuard.get(), decoderId, data, pts, flag, isVideo);
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

uint64_t NativeHardwareDecoder::requestVideoFrame(std::string_view decoderId,
                                                  uint64_t waitTime,
                                                  uint32_t width,
                                                  uint32_t height) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return 0;
    }
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_requestVideoFrame(envGuard.get(),
                                                    decoderId,
                                                    waitTime,
                                                    width,
                                                    height);
}

} // namespace slark
