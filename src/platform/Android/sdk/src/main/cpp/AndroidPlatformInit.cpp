//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

#include "AAudioRender.h"
#include "AndroidEGLContext.h"
#include "Base.h"
#include "HttpsHelper.h"
#include "Log.hpp"
#include "MediaDefs.h"
#include "PlatformRegistry.h"
#include "VideoHardwareDecoder.h"

DISABLE_WARNINGS
#include <openssl/ssl.h>
#include <openssl/x509.h>
ENABLE_WARNINGS

namespace slark {

namespace {

// Android 系统 CA 证书目录
constexpr const char* kSystemCaDir = "/system/etc/security/cacerts";

// 从 Android 系统 CA 目录加载所有证书到 SSL_CTX
void loadAndroidSystemCerts(SSL_CTX* ctx) {
    DIR* dir = opendir(kSystemCaDir);
    if (!dir) {
        LogW("Failed to open system CA dir: {}", kSystemCaDir);
        return;
    }

    auto* store = SSL_CTX_get_cert_store(ctx);
    if (!store) {
        LogW("Failed to get cert store from SSL_CTX");
        closedir(dir);
        return;
    }

    int loadedCount = 0;
    struct dirent* entry;
    char path[PATH_MAX];

    while ((entry = readdir(dir)) != nullptr) {
        // Android CA 证书文件以数字命名（hash.0）
        if (entry->d_name[0] == '.') {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", kSystemCaDir, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        FILE* fp = fopen(path, "rb");
        if (!fp) {
            continue;
        }

        // Android 系统证书可能有多种格式，尝试 PEM 和 DER
        X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
        if (!cert) {
            rewind(fp);
            cert = d2i_X509_fp(fp, nullptr);
        }
        fclose(fp);

        if (cert) {
            if (X509_STORE_add_cert(store, cert) == 1) {
                loadedCount++;
            }
            X509_free(cert);
        }
    }
    closedir(dir);

    LogI("Loaded {} system CA certificates from {}", loadedCount, kSystemCaDir);
}

struct AndroidPlatformRegistrar {
    AndroidPlatformRegistrar() noexcept {
        auto& registry = PlatformRegistry::instance();

        registry.registerAudioRenderFactory([](const std::shared_ptr<AudioInfo>& info) {
            return std::make_shared<AAudioRender>(info);
        });

        registry.registerGLContextFactory([]() -> IEGLContextRefPtr {
            return std::make_shared<AndroidEGLContext>();
        });

        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_AUDIO_AAC) + "HW",
                                   DecoderType::AACHardwareDecoder);

        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_VIDEO_AVC) + "HW",
                                   DecoderType::VideoHardwareDecoder);
        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_VIDEO_HEVC) + "HW",
                                   DecoderType::VideoHardwareDecoder);
        registry.registerDecoderInfo(DecoderType::VideoHardwareDecoder,
                                     VideoHardwareDecoder::info());

        // 配置 HTTPS CA 证书：从 Android 系统证书目录加载
        // SSLManager::shareContext() 在首次 SSL 连接时调用 configContext
        http::HttpsHelper::configContext = [](http::SSLContextPtr& ctx) {
            loadAndroidSystemCerts(ctx.get());
        };
    }
};

static AndroidPlatformRegistrar androidPlatformRegistrar;

}  // namespace

std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo) {
    return PlatformRegistry::instance().createAudioRender(audioInfo);
}

}  // namespace slark
