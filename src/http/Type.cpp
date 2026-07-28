//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Type.h"

#include <sstream>

namespace slark::http {

std::string RequestPerf::summary() const {
    std::ostringstream oss;
    oss << "[RequestPerf] host=" << host << " total=" << totalRequestTimeMs << "ms";

    oss << " | conn: reuse=" << (connection.reuseAttempted ? "yes" : "no");
    if (connection.reuseAttempted) {
        oss << "(" << (connection.reuseSucceeded ? "hit" : "miss") << ")";
    }
    oss << " connect=" << connection.connectTimeMs << "ms"
        << " tls=" << connection.tlsHandshakeTimeMs << "ms";

    oss << " | transport: send_retry=" << transport.sendRetryCount
        << " recv_wait=" << transport.receiveWaitCount << " send=" << transport.totalSendTimeMs
        << "ms"
        << " recv=" << transport.totalReceiveTimeMs << "ms";

    if (transport.timeoutSource != TimeoutSource::None) {
        const char* source = "unknown";
        switch (transport.timeoutSource) {
            case TimeoutSource::Connect:
                source = "connect";
                break;
            case TimeoutSource::TlsHandshake:
                source = "tls";
                break;
            case TimeoutSource::Write:
                source = "write";
                break;
            case TimeoutSource::ReadIdle:
                source = "read_idle";
                break;
            case TimeoutSource::ReadOverall:
                source = "read_overall";
                break;
            default:
                break;
        }
        oss << " timeout=" << source;
    }

    oss << " | parser: header=" << parser.headerParseTimeUs << "us"
        << " chunks=" << parser.chunkCount << " body=" << parser.bodySize << "bytes";

    return oss.str();
}

}  // namespace slark::http
