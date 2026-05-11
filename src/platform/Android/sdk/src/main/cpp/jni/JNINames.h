//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <array>

namespace slark::JNI {

namespace JavaClass {
inline constexpr const char* SlarkNativeBridge = "com/slark/sdk/SlarkNativeBridge";
inline constexpr const char* SlarkPlayerManager = "com/slark/sdk/SlarkPlayerManager";
inline constexpr const char* SlarkPlayerState = "com/slark/api/SlarkPlayerState";
inline constexpr const char* SlarkPlayerEvent = "com/slark/api/SlarkPlayerEvent";
inline constexpr const char* MediaCodecDecoder = "com/slark/sdk/MediaCodecDecoder";
inline constexpr const char* AudioPlayer = "com/slark/sdk/AudioPlayer";
inline constexpr const char* AudioPlayerAction = "com/slark/sdk/AudioPlayer$Action";
inline constexpr const char* AudioPlayerConfig = "com/slark/sdk/AudioPlayer$Config";
inline constexpr const char* RenderTexture = "com/slark/sdk/RenderTexture";
inline constexpr const char* AudioDecoderConfig = "com/slark/sdk/AudioDecoderConfig";
inline constexpr const char* VideoDecoderConfig = "com/slark/sdk/VideoDecoderConfig";
inline constexpr const char* DecodeMode = "com/slark/sdk/DecodeMode";
inline constexpr const char* AndroidContext = "android/content/Context";
inline constexpr const char* JavaFile = "java/io/File";
} // namespace JavaClass

namespace JavaMethod {
inline constexpr const char* Logout = "logout";
inline constexpr const char* NotifyPlayerState = "notifyPlayerState";
inline constexpr const char* NotifyPlayerEvent = "notifyPlayerEvent";
inline constexpr const char* NotifyPlayerTime = "notifyPlayerTime";
inline constexpr const char* RequestRender = "requestRender";
inline constexpr const char* CreateAudioPlayer = "createAudioPlayer";
inline constexpr const char* AudioPlayerAction = "audioPlayerAction";
inline constexpr const char* AudioPlayerConfig = "audioPlayerConfig";
inline constexpr const char* GetPlayedTime = "getPlayedTime";
inline constexpr const char* SendPacket = "sendPacket";
inline constexpr const char* CreateVideoDecoder = "createVideoDecoder";
inline constexpr const char* CreateAudioDecoder = "createAudioDecoder";
inline constexpr const char* ReleaseDecoder = "releaseDecoder";
inline constexpr const char* FlushDecoder = "flushDecoder";
inline constexpr const char* RequestVideoFrame = "requestVideoFrame";
inline constexpr const char* GetFilesDir = "getFilesDir";
inline constexpr const char* GetCacheDir = "getCacheDir";
inline constexpr const char* GetPath = "getPath";
} // namespace JavaMethod

namespace JavaEnumValue {
inline constexpr const char* Texture = "Texture";
inline constexpr const char* ByteBuffer = "ByteBuffer";
inline constexpr const char* Play = "Play";
inline constexpr const char* Pause = "Pause";
inline constexpr const char* Flush = "Flush";
inline constexpr const char* Release = "Release";
inline constexpr const char* Rate = "Rate";
inline constexpr const char* Volume = "Volume";
} // namespace JavaEnumValue

namespace JavaSignature {
inline constexpr const char* NotifyPlayerState =
    "(Ljava/lang/String;Lcom/slark/api/SlarkPlayerState;)V";
inline constexpr const char* NotifyPlayerEvent =
    "(Ljava/lang/String;Lcom/slark/api/SlarkPlayerEvent;Ljava/lang/String;)V";
inline constexpr const char* NotifyPlayerTime = "(Ljava/lang/String;D)V";
inline constexpr const char* GetFileDir = "()Ljava/io/File;";
inline constexpr const char* GetPath = "()Ljava/lang/String;";
} // namespace JavaSignature

struct JavaMethodPreloadInfo {
    const char* className;
    const char* methodName;
    const char* signature;
    bool isStatic;
};

namespace JavaPreload {
inline constexpr std::array Classes = {
    JavaClass::SlarkNativeBridge,
    JavaClass::SlarkPlayerManager,
    JavaClass::SlarkPlayerState,
    JavaClass::SlarkPlayerEvent,
    JavaClass::MediaCodecDecoder,
    JavaClass::AudioPlayer,
    JavaClass::AudioPlayerAction,
    JavaClass::AudioPlayerConfig,
    JavaClass::RenderTexture,
    JavaClass::AudioDecoderConfig,
    JavaClass::VideoDecoderConfig,
    JavaClass::DecodeMode,
    JavaClass::AndroidContext,
    JavaClass::JavaFile,
};

inline constexpr std::array Methods = {
    JavaMethodPreloadInfo{JavaClass::SlarkPlayerManager,
                          JavaMethod::NotifyPlayerState,
                          JavaSignature::NotifyPlayerState,
                          true},
    JavaMethodPreloadInfo{JavaClass::SlarkPlayerManager,
                          JavaMethod::NotifyPlayerEvent,
                          JavaSignature::NotifyPlayerEvent,
                          true},
    JavaMethodPreloadInfo{JavaClass::SlarkPlayerManager,
                          JavaMethod::NotifyPlayerTime,
                          JavaSignature::NotifyPlayerTime,
                          true},
    JavaMethodPreloadInfo{JavaClass::AndroidContext,
                          JavaMethod::GetFilesDir,
                          JavaSignature::GetFileDir,
                          false},
    JavaMethodPreloadInfo{JavaClass::AndroidContext,
                          JavaMethod::GetCacheDir,
                          JavaSignature::GetFileDir,
                          false},
    JavaMethodPreloadInfo{JavaClass::JavaFile, JavaMethod::GetPath, JavaSignature::GetPath, false},
};
} // namespace JavaPreload

} // namespace slark::JNI
