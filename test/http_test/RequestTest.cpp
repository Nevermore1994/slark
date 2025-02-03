//
// Created by Nevermore on 2024/12/6.
// slark RequestTest
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include <gtest/gtest.h>
#include <condition_variable>
#include "Data.hpp"
#include "Request.h"
#include "Type.h"

using namespace slark::http;


TEST(Request, Get) {
    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;
    RequestInfo info;
    info.url = "https://httpbin.org/get";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent",   "runscope/0.1"},
                    {"Accept",       "*/*"}};
    ResponseHandler handler;
    handler.onParseHeaderDone = [](const RequestInfo& , http::ResponseHeader&& header) {
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler.onError = [](const RequestInfo& , ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
    };
    handler.onCompleted = [&](const RequestInfo& ) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    Request request(std::move(info), std::move(handler));
    std::unique_lock lock(mutex);
    cond.wait(lock, [&] { return isFinished; });
}

TEST(Request, Post) {
    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;
    RequestInfo info;
    info.url = "https://httpbin.org/post";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent",   "runscope/0.1"},
                    {"Accept",       "*/*"}};
    info.body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    ResponseHandler handler;
    handler.onParseHeaderDone = [](const RequestInfo& , http::ResponseHeader&& header) {
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler.onError = [](const RequestInfo& , ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
    };
    handler.onCompleted = [&](const RequestInfo& ) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    Request request(std::move(info), std::move(handler));
    std::unique_lock lock(mutex);
    cond.wait(lock, [&] { return isFinished; });
}

TEST(Request, Put) {
    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;
    RequestInfo info;
    info.url = "https://httpbin.org/put";
    info.methodType = HttpMethodType::Put;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent",   "runscope/0.1"},
                    {"Accept",       "*/*"}};
    info.body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    ResponseHandler handler;
    handler.onParseHeaderDone = [](const RequestInfo& , http::ResponseHeader&& header) {
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler.onError = [](const RequestInfo& , ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
    };
    handler.onCompleted = [&](const RequestInfo&) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    Request request(std::move(info), std::move(handler));
    std::unique_lock lock(mutex);
    cond.wait(lock, [&] { return isFinished; });
}

TEST(Request, Delete) {

    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;
    RequestInfo info;
    info.url = "https://httpbin.org/delete";
    info.methodType = HttpMethodType::Delete;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent",   "runscope/0.1"},
                    {"Accept",       "*/*"}};
    ResponseHandler handler;
    handler.onParseHeaderDone = [](const RequestInfo&, http::ResponseHeader&& header) {
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler.onError = [](const RequestInfo& , ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
    };
    handler.onCompleted = [&](const RequestInfo& ) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    Request request(std::move(info), std::move(handler));
    std::unique_lock lock(mutex);
    cond.wait(lock, [&] { return isFinished; });
}

TEST(Request, Patch) {
    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;
    RequestInfo info;
    info.url = "https://httpbin.org/patch";
    info.methodType = HttpMethodType::Patch;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent",   "runscope/0.1"},
                    {"Accept",       "*/*"}};
    info.body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    ResponseHandler handler;
    handler.onParseHeaderDone = [](const RequestInfo& , http::ResponseHeader&& header) {
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler.onError = [](const RequestInfo& , ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
    };
    handler.onCompleted = [&](const RequestInfo& ) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    Request request(std::move(info), std::move(handler));
    std::unique_lock lock(mutex);
    cond.wait(lock, [&] { return isFinished; });
}

TEST(Request, Options) {
    std::condition_variable cond;
    std::mutex mutex;
    bool isFinished = false;
    RequestInfo info;
    info.url = "https://httpbin.org/anything";
    info.methodType = HttpMethodType::Options;
    info.headers = {{"Content-Type",                   "application/json"},
                    {"User-Agent",                     "runscope/0.1"},
                    {"Accept",                         "*/*"},
                    {"Access-Control-Request-Method",  "POST"},
                    {"Access-Control-Request-Headers", "X-PINGOTHER, Content-Type"}};
    ResponseHandler handler;

    handler.onParseHeaderDone = [](const RequestInfo& , http::ResponseHeader&& header) {
        ASSERT_EQ(header.httpStatusCode, HttpStatusCode::OK);
    };
    handler.onError = [](const RequestInfo& , ErrorInfo info) {
        ASSERT_EQ(info.retCode, ResultCode::Success);
    };
    handler.onCompleted = [&](const RequestInfo& ) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
        }
        cond.notify_all();
    };
    Request request(std::move(info), std::move(handler));
    std::unique_lock lock(mutex);
    cond.wait(lock, [&] { return isFinished; });
}
