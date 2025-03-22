//
// Created by Nevermore on 2025/3/6.
//

#include "NativeHardwareDecoder.h"
#include "SlarkNative.h"
#include "NativeDecoderManager.h"
#include "AVFrame.hpp"
#include "VideoHardwareDecoder.h"

using namespace slark;
constexpr std::string_view kMediaCodecDecoderClass = "com/slark/sdk/MediaCodecDecoder";
std::string Native_HardwareDecoder_createVideoDecoder(JNIEnv* env, std::string_view mediaInfo,
                                                jint width,
                                                jint height) {
    auto decoderClass = env->FindClass(kMediaCodecDecoderClass.data());
    if (decoderClass == nullptr) {
        LogE("create decoder failed, not found video decoder class");
        return "";
    }
    jmethodID createDecoderMethod = env->GetStaticMethodID(decoderClass, "createVideoDecoder", "(Ljava/lang/String;II)Ljava/lang/String;");
    if (createDecoderMethod == nullptr) {
        LogE("create decoder failed, not found decoder method");
        return "";
    }
    jstring jMediaInfo = env->NewStringUTF(mediaInfo.data());
    auto jDecoderId = (jstring)env->CallStaticObjectMethod(decoderClass, createDecoderMethod, jMediaInfo, width, height);
    auto cDecoderId = env->GetStringUTFChars(jDecoderId, nullptr);
    auto decoderId = std::string(cDecoderId);
    env->ReleaseStringUTFChars(jDecoderId, cDecoderId);
    return decoderId;
}

std::string Native_HardwareDecoder_createAudioDecoder(JNIEnv* env, std::string_view mediaInfo,
                                                      int32_t sampleRate, uint8_t channelCount,
                                                      uint8_t profile) {
    auto decoderClass = env->FindClass(kMediaCodecDecoderClass.data());
    if (decoderClass == nullptr) {
        LogE("create decoder failed, not found audio decoder class");
        return "";
    }
    jmethodID createDecoderMethod = env->GetStaticMethodID(decoderClass, "createAudioDecoder", "(Ljava/lang/String;III)Ljava/lang/String;");
    if (createDecoderMethod == nullptr) {
        LogE("create decoder failed, not found decoder method");
        return "";
    }
    jstring jMediaInfo = env->NewStringUTF(mediaInfo.data());
    auto jDecoderId = (jstring)env->CallStaticObjectMethod(decoderClass, createDecoderMethod, jMediaInfo, sampleRate, channelCount, profile);
    auto cDecoderId = env->GetStringUTFChars(jDecoderId, nullptr);
    auto decoderId = std::string(cDecoderId);
    env->ReleaseStringUTFChars(jDecoderId, cDecoderId);
    return decoderId;
}


void Native_HardwareDecoder_sendData(JNIEnv* env, std::string_view decoderId, DataPtr data,
                                     uint64_t pts, NativeDecodeFlag flag) {
    if (data == nullptr || data->empty()) {
        return;
    }
    auto decoderClass = env->FindClass(kMediaCodecDecoderClass.data());
    if (decoderClass == nullptr) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }
    jmethodID methodId = env->GetStaticMethodID(decoderClass, "sendData", "(Ljava/lang/String;[BJI)V");
    if (methodId == nullptr) {
        LogE("create decoder failed, not found send data method");
        return;
    }
    auto size = static_cast<jsize>(data->length);
    jbyteArray dataArray = env->NewByteArray(size);
    if (dataArray == nullptr) {
        LogE("create data array error");
        return;
    }
    jstring jDecodeId = env->NewStringUTF(decoderId.data());
    env->SetByteArrayRegion(dataArray, 0, size, reinterpret_cast<jbyte*>(data->rawData));
    env->CallStaticVoidMethod(decoderClass, methodId,
                              jDecodeId, dataArray, static_cast<jlong>(pts), static_cast<jint>(flag));
}

void Native_HardwareDecoder_releaseDecoder(JNIEnv* env, std::string_view decoderId) {
    auto decoderClass = env->FindClass(kMediaCodecDecoderClass.data());
    if (decoderClass == nullptr) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }
    jmethodID methodId = env->GetStaticMethodID(decoderClass, "releaseDecoder", "(Ljava/lang/String;)V");
    if (methodId == nullptr) {
        LogE("create decoder failed, not found decoder method");
        return;
    }
    jstring jDecodeId = env->NewStringUTF(decoderId.data());
    env->CallStaticVoidMethod(decoderClass, methodId, jDecodeId);
}

void Native_HardwareDecoder_flush(JNIEnv* env, std::string_view decoderId) {
    constexpr std::string_view kActionClass = "com/slark/sdk/MediaCodecDecoder$Action";
    jclass actionClass = env->FindClass(kActionClass.data());
    if (actionClass == nullptr) {
        LogE("create decoder failed, not found video decoder class");
        return;
    }

    jstring jDecoderId = env->NewStringUTF(decoderId.data());
    jfieldID fieldId = env->GetStaticFieldID(actionClass, "FLUSH", "Lcom/slark/sdk/MediaCodecDecoder$Action;");
    jobject enumValue = env->GetStaticObjectField(actionClass, fieldId);
    jclass decoderClass = env->FindClass("com/slark/sdk/MediaCodecDecoder");
    jmethodID jMethodId = env->GetStaticMethodID(decoderClass, "doAction", "(Ljava/lang/String;Lcom/slark/sdk/MediaCodecDecoder$Action;)V");
    env->CallStaticVoidMethod(decoderClass, jMethodId, jDecoderId, enumValue);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_MediaCodecDecoder_processRawData(JNIEnv* env, jobject /* thiz */,
                                                 jstring decoderId,
                                                 jbyteArray byteBuffer,
                                                 jlong pts) {
    using namespace slark;
    LogI("pushFrameDecode success:{}", pts);
    auto cDecoderId= env->GetStringUTFChars(decoderId, nullptr);
    auto decoderIdStr = std::string(cDecoderId);
    env->ReleaseStringUTFChars(decoderId, cDecoderId);
    auto length = env->GetArrayLength(byteBuffer);
    auto dataPtr = std::make_unique<Data>(length);
    env->GetByteArrayRegion(byteBuffer, 0, length, reinterpret_cast<jbyte*>(dataPtr->rawData));
    dataPtr->length = length;
    //send to manager
    auto decoder = NativeDecoderManager::shareInstance().find(decoderIdStr);
    if (!decoder) {
        LogE("not found decoder");
        return;
    }
    auto frame = decoder->getDecodingFrame(pts);
    if (!frame) {
        LogE("not found frame");
        return;
    }
    frame->data = std::move(dataPtr);
    if (decoder->isVideo()) {
        std::dynamic_pointer_cast<VideoFrameInfo>(frame->info)->format = FrameFormat::MediaCodec;
    }
}

namespace slark {

std::string
NativeHardwareDecoder::createVideo(const std::string& mediaInfo, int32_t width, int32_t height) {
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_createVideoDecoder(envGuard.get(), mediaInfo, width, height);
}

std::string
NativeHardwareDecoder::createAudio(const std::string &mediaInfo, uint64_t sampleRate,
                                   uint8_t channelCount, uint8_t profile) {
    JNIEnvGuard envGuard(getJavaVM());
    return Native_HardwareDecoder_createAudioDecoder(envGuard.get(), mediaInfo,
                                                     static_cast<int32_t>(sampleRate),
                                                     channelCount, profile);
}

void NativeHardwareDecoder::sendData(std::string_view decoderId, DataPtr data, uint64_t pts, NativeDecodeFlag flag) {
    if (decoderId.empty()) {
        LogE("decoderId is empty");
        return;
    }
    JNIEnvGuard envGuard(getJavaVM());
    Native_HardwareDecoder_sendData(envGuard.get(), decoderId, std::move(data), pts, flag);
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