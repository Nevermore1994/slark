//
// Created by Nevermore on 2025/3/6.
//

#include "NativeHardwareDecoder.h"
#include "SlarkNative.h"
#include "NativeDecoderManager.h"
#include "AVFrame.hpp"
#include "JNICache.h"
#include "JNISignature.h"
#include "JNIHelper.h"

using namespace slark;
using namespace slark::JNI;
constexpr std::string_view kMediaCodecDecoderClass = "com/slark/sdk/MediaCodecDecoder";

std::string Native_HardwareDecoder_createVideoDecoder(JNIEnv* env,
                                                     const std::shared_ptr<VideoDecoderConfig>& decoderConfig) {
    if (!decoderConfig) {
        LogE("config is nullptr");
        return {};
    }
    auto decoderClass= JNICache::shareInstance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return {};
    }
    auto methodSignature = JNI::makeJNISignature(JNI::String, JNI::String, JNI::Int, JNI::Int);
    auto createDecoderMethod = JNICache::shareInstance().getStaticMethodId(decoderClass,
                                                                           "createVideoDecoder",
                                                                           methodSignature);
    if (!createDecoderMethod) {
        LogE("create decoder failed, not found decoder method");
        return {};
    }
    auto jMediaInfo= ToJVM::toString(env, decoderConfig->mediaInfo);
    auto jDecoderId = reinterpret_cast<jstring>(env->CallStaticObjectMethod(decoderClass.get(),
                                                           createDecoderMethod.get(),
                                                           jMediaInfo.get(),
                                                           decoderConfig->width,
                                                           decoderConfig->height));
    return FromJVM::toString(env, jDecoderId);
}

std::string Native_HardwareDecoder_createAudioDecoder(JNIEnv* env,
                                                      const std::shared_ptr<AudioDecoderConfig>& decoderConfig) {
    auto decoderClass = JNICache::shareInstance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found audio decoder class");
        return "";
    }
    auto methodSignature= JNI::makeJNISignature(JNI::String, JNI::String, JNI::Int, JNI::Int, JNI::Int);
    auto createDecoderMethod = JNICache::shareInstance().getStaticMethodId(decoderClass,
                                                                           "createAudioDecoder",
                                                                           methodSignature);
    if (!createDecoderMethod) {
        LogE("create decoder failed, not found decoder method");
        return "";
    }
    jstring jMediaInfo = env->NewStringUTF(decoderConfig->mediaInfo.data());
    auto jDecoderId = reinterpret_cast<jstring>(env->CallStaticObjectMethod(decoderClass.get(),
                                                           createDecoderMethod.get(),
                                                           jMediaInfo,
                                                           decoderConfig->sampleRate,
                                                           decoderConfig->channels,
                                                           decoderConfig->profile));
    auto decoderId = FromJVM::toString(env, jDecoderId);
    return decoderId;
}

DecoderErrorCode Native_HardwareDecoder_sendPacket(JNIEnv* env, std::string_view decoderId,
                                                   DataPtr& data, uint64_t pts, NativeDecodeFlag flag) {
    if (data == nullptr || data->empty()) {
        LogE("data error");
        return DecoderErrorCode::InputDataError;
    }
    auto decoderClass = JNICache::shareInstance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return DecoderErrorCode::Unknown;
    }
    auto methodSignature = JNI::makeJNISignature(JNI::Int, JNI::String, JNI::makeArray(JNI::Byte), JNI::Long, JNI::Int);
    auto methodId = JNICache::shareInstance().getStaticMethodId(decoderClass,
                                                                "sendPacket",
                                                                methodSignature);
    if (!methodId) {
        LogE("create decoder failed, not found send data method");
        return DecoderErrorCode::Unknown;
    }
    auto byteArray = ToJVM::toByteArray(env, *data);
    jstring jDecodeId = env->NewStringUTF(decoderId.data());
    int res = env->CallStaticIntMethod(decoderClass.get(), methodId.get(),
                              jDecodeId, byteArray.get(), static_cast<jlong>(pts), static_cast<jint>(flag));
    return static_cast<DecoderErrorCode>(res);
}

void Native_HardwareDecoder_releaseDecoder(JNIEnv* env, std::string_view decoderId) {
    auto decoderClass = JNICache::shareInstance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }
    auto methodSignature = JNI::makeJNISignature(JNI::Void, JNI::String);
    auto methodId = JNICache::shareInstance().getStaticMethodId(decoderClass,
                                                                "releaseDecoder",
                                                                methodSignature);
    if (!methodId) {
        LogE("create decoder failed, not found decoder method");
        return;
    }
    jstring jDecodeId = env->NewStringUTF(decoderId.data());
    env->CallStaticVoidMethod(decoderClass.get(), methodId.get(), jDecodeId);
}

void Native_HardwareDecoder_flush(JNIEnv* env, std::string_view decoderId) {
    constexpr std::string_view kActionClass = "com/slark/sdk/MediaCodecDecoder$Action";
    auto decoderClass = JNICache::shareInstance().getClass(env, kMediaCodecDecoderClass);
    if (!decoderClass) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }
    jstring jDecoderId = env->NewStringUTF(decoderId.data());
    auto enumValue = JNICache::shareInstance().getEnumField(env, kActionClass, "FLUSH");
    auto methodSignature = JNI::makeJNISignature(JNI::Void, JNI::String, JNI::makeObject(kActionClass));
    auto methodId= JNICache::shareInstance().getStaticMethodId(decoderClass,
                                                               "doAction",
                                                               methodSignature);
    env->CallStaticVoidMethod(decoderClass.get(), methodId.get(), jDecoderId, enumValue.get());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_MediaCodecDecoder_processRawData(JNIEnv* env, jobject /* thiz */,
                                                 jstring jDecoderId,
                                                 jbyteArray byteBuffer,
                                                 jlong pts) {
    using namespace slark;
    LogI("native decode success:{}", pts);
    auto decoderId = FromJVM::toString(env, jDecoderId);
    //send to manager
    auto decoder = NativeDecoderManager::shareInstance().find(decoderId);
    if (!decoder) {
        LogE("not found decoder");
        return;
    }
    auto frame = decoder->getDecodingFrame(static_cast<uint64_t>(pts));
    if (!frame) {
        LogE("not found frame");
        return;
    }
    auto dataPtr = FromJVM::toData(env, byteBuffer);
    frame->data = std::move(dataPtr);
    if (decoder->isVideo()) {
        std::dynamic_pointer_cast<VideoFrameInfo>(frame->info)->format = FrameFormat::MediaCodec;
    }
}

namespace slark {

std::string
NativeHardwareDecoder::createVideo(const std::shared_ptr<VideoDecoderConfig>& decoderConfig) {
    if (!decoderConfig) {
        return {};
    }
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_createVideoDecoder(envGuard.get(), decoderConfig);
}

std::string
NativeHardwareDecoder::createAudio(const std::shared_ptr<AudioDecoderConfig>& decoderConfig ) {
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_createAudioDecoder(envGuard.get(), decoderConfig);
}

DecoderErrorCode NativeHardwareDecoder::sendPacket(std::string_view decoderId, DataPtr& data,
                                                   uint64_t pts, NativeDecodeFlag flag) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return DecoderErrorCode::NotFoundDecoder;
    }
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_sendPacket(envGuard.get(), decoderId, data, pts, flag);
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

}