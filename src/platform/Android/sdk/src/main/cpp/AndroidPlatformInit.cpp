//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

#include "AAudioRender.h"
#include "Base.h"
#include "AudioRenderSink.h"
#include "HttpsHelper.h"
#include "Log.hpp"
#include "ScopeGuard.h"

DISABLE_WARNINGS
#include <openssl/ssl.h>
#include <openssl/x509.h>
ENABLE_WARNINGS

namespace slark {

namespace {

// Android system CA certificate directory
constexpr const char* kSystemCaDir = "/system/etc/security/cacerts";

// Load all certificates from the Android system CA directory into the SSL_CTX
void loadAndroidSystemCerts(SSL_CTX* ctx) {
    DIR* dir = opendir(kSystemCaDir);
    if (!dir) {
        LogW("Failed to open system CA dir: {}", kSystemCaDir);
        return;
    }
    ScopeGuard dirGuard([dir] { closedir(dir); });

    auto* store = SSL_CTX_get_cert_store(ctx);
    if (!store) {
        LogW("Failed to get cert store from SSL_CTX");
        return;
    }

    int loadedCount = 0;
    struct dirent* entry;
    char path[PATH_MAX];

    while ((entry = readdir(dir)) != nullptr) {
        // Android CA files are named by hash (hash.0)
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

        // Android system certs may come in several formats; try PEM and DER
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

    LogI("Loaded {} system CA certificates from {}", loadedCount, kSystemCaDir);
}

struct AndroidHttpsConfigurator {
    AndroidHttpsConfigurator() noexcept {
        http::HttpsHelper::configContext = [](http::SSLContextPtr& ctx) {
            loadAndroidSystemCerts(ctx.get());
        };
    }
};

static AndroidHttpsConfigurator androidHttpsConfigurator;

// Hands the engine's AudioRenderSink the platform AAudio render at static-init time.
struct AndroidAudioRenderFactoryRegistration {
    AndroidAudioRenderFactoryRegistration() {
        engine::setPlatformAudioRenderFactory(
            [](const std::shared_ptr<AudioInfo>& info) -> std::shared_ptr<IAudioRender> {
                return createAudioRender(info);
            });
    }
};

static AndroidAudioRenderFactoryRegistration androidAudioRenderFactoryRegistration;

}  // namespace

std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo) {
    return std::make_shared<AAudioRender>(audioInfo);
}

}  // namespace slark
