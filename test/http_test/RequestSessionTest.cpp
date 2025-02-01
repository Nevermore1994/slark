//
// Created by Nevermore on 2025/2/1.
// slark RequestSession
// Copyright (c) 2025 Nevermore All rights reserved.
//

#include <gtest/gtest.h>
#include <condition_variable>
#include "Data.hpp"
#include "Request.h"
#include "Type.h"

using namespace slark::http;


TEST(RequestSession, MultipleRequests) {
    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;

    auto handler = std::make_unique<ResponseHandler>();
    handler->onParseHeaderDone = [](std::string_view, http::ResponseHeader&& header) {
        std::cout << "header:" << static_cast<int>(header.httpStatusCode) << std::endl;
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler->onError = [&](std::string_view, ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    handler->onDisconnected = [&](std::string_view) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };

    RequestSession session(std::move(handler));

    // GET Request
    {
        isFinished = false;
        auto info = std::make_unique<RequestInfo>();
        info->url = "https://httpbin.org/get";
        info->methodType = HttpMethodType::Get;
        info->headers = {{"Content-Type", "application/json"},
                         {"User-Agent",   "runscope/0.1"},
                         {"Accept",       "*/*"}};

        std::string reqId = session.request(std::move(info));
        std::unique_lock lock(mutex);
        cond.wait(lock, [&] { return isFinished; });
        ASSERT_FALSE(reqId.empty());
        std::cout << "GET Request Done" << std::endl;
    }

    // DELETE Request
    {
        isFinished = false;
        std::unique_ptr<RequestInfo> info = std::make_unique<RequestInfo>();
        info->url = "https://httpbin.org/delete";
        info->methodType = HttpMethodType::Delete;
        info->headers = {{"Content-Type", "application/json"},
                         {"User-Agent",   "runscope/0.1"},
                         {"Accept",       "*/*"}};

        std::string reqId = session.request(std::move(info));
        std::unique_lock lock(mutex);
        cond.wait(lock, [&] { return isFinished; });
        ASSERT_FALSE(reqId.empty());
        std::cout << "DELETE Request Done" << std::endl;
    }

    // PATCH Request
    {
        isFinished = false;
        std::unique_ptr<RequestInfo> info = std::make_unique<RequestInfo>();
        info->url = "https://httpbin.org/patch";
        info->methodType = HttpMethodType::Patch;
        info->headers = {{"Content-Type", "application/json"},
                         {"User-Agent",   "runscope/0.1"},
                         {"Accept",       "*/*"}};
        info->body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");

        std::string reqId = session.request(std::move(info));
        std::unique_lock lock(mutex);
        cond.wait(lock, [&] { return isFinished; });
        ASSERT_FALSE(reqId.empty());
        std::cout << "PATCH Request Done" << std::endl;
    }

    session.close();
}