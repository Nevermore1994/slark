//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "CallbackBridge.hpp"

namespace slark::http::detail {

void dispatchResponse(HttpResponse& response,
                      const RequestInfo& requestInfo,
                      HttpResponseHandler& handler) {
    if (handler.onConnected) {
        handler.onConnected(requestInfo);
    }

    if (handler.onHeader) {
        ResponseHeader header;
        header.httpStatusCode = response.statusCode;
        header.headers = response.headers;
        header.reasonPhrase = response.reasonPhrase;
        handler.onHeader(requestInfo, std::move(header));
    }

    if (handler.onData && response.body) {
        handler.onData(requestInfo, std::move(response.body));
    }

    if (response.isSuccess()) {
        if (handler.onCompleted) {
            handler.onCompleted(requestInfo);
        }
    } else {
        if (handler.onError) {
            handler.onError(requestInfo, response.error);
        }
    }
}

}  // namespace slark::http::detail
