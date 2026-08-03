#include "RedirectPolicy.h"

#include <gtest/gtest.h>

#include "StringUtil.h"
#include "Type.h"

using namespace slark;
using namespace slark::http;

TEST(RedirectPolicyTest, ExtractLocationFromHeaders) {
    RedirectPolicy policy;
    std::unordered_map<std::string, std::string> h = {{"location", "http://example.com/new"}};
    auto loc = policy.extractRedirectLocation(h);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(*loc, "http://example.com/new");
}

TEST(RedirectPolicyTest, ExtractLocationMissing) {
    RedirectPolicy policy;
    std::unordered_map<std::string, std::string> h;
    auto loc = policy.extractRedirectLocation(h);
    EXPECT_FALSE(loc.has_value());
}

TEST(RedirectPolicyTest, ExtractLocationCaseInsensitive) {
    RedirectPolicy policy;
    std::unordered_map<std::string, std::string> h = {{"Location", "http://example.org"}};
    auto loc = policy.extractRedirectLocation(h);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(*loc, "http://example.org");
}

TEST(RedirectPolicyTest, ShouldRedirectWithValidCode) {
    RedirectPolicy policy;
    HttpResponse resp;
    resp.statusCode = HttpStatusCode::Found;
    resp.headers["location"] = "http://example.com/new";
    RequestInfo req;
    req.url = "http://example.com/old";
    EXPECT_TRUE(policy.shouldRedirect(resp, req, 0));
}

TEST(RedirectPolicyTest, ShouldNotRedirectWithoutLocation) {
    // 修复（C-09）：shouldRedirect 仅判定状态码，Location 是否存在由调用方
    // extractRedirectLocation 校验；无 Location 时 processStreaming 不应跟随。
    // 本用例验证：仅状态码时 shouldRedirect 返回 true 但 Location 提取为空。
    RedirectPolicy policy;
    HttpResponse resp;
    resp.statusCode = HttpStatusCode::Found;
    RequestInfo req;
    req.url = "http://example.com/old";
    EXPECT_TRUE(policy.shouldRedirect(resp, req, 0));
    EXPECT_FALSE(policy.extractRedirectLocation(resp.headers).has_value());
}

TEST(RedirectPolicyTest, ShouldNotRedirectExceedMaxCount) {
    RedirectPolicy policy;
    HttpResponse resp;
    resp.statusCode = HttpStatusCode::Found;
    resp.headers["location"] = "http://example.com/new";
    RequestInfo req;
    req.url = "http://example.com/old";
    EXPECT_FALSE(policy.shouldRedirect(resp, req, 10));
}

TEST(RedirectPolicyTest, ShouldNotRedirectNonRedirectCode) {
    RedirectPolicy policy;
    HttpResponse resp;
    resp.statusCode = HttpStatusCode::OK;
    resp.headers["location"] = "http://example.com/new";
    RequestInfo req;
    req.url = "http://example.com/old";
    EXPECT_FALSE(policy.shouldRedirect(resp, req, 0));
}

TEST(RedirectPolicyTest, BuildRedirectPreservesMethod) {
    RequestInfo req;
    req.url = "http://example.com/old";
    req.methodType = HttpMethodType::Post;
    auto result = RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, "http://example.com/new");
    EXPECT_EQ(result->methodType, HttpMethodType::Post);
}

TEST(RedirectPolicyTest, SeeOtherChangesMethodToGet) {
    RequestInfo req;
    req.url = "http://example.com/old";
    req.methodType = HttpMethodType::Post;
    auto result = RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::SeeOther, "/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->methodType, HttpMethodType::Get);
}

TEST(RedirectPolicyTest, RejectHttpsDowngrade) {
    RequestInfo req;
    req.url = "https://example.com/old";
    auto result =
        RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "http://other.com/new");
    EXPECT_FALSE(result.has_value());
}

TEST(RedirectPolicyTest, CrossOriginStripsSensitiveHeaders) {
    RequestInfo req;
    req.url = "http://example.com/old";
    req.headers["Authorization"] = "Bearer token";
    auto result =
        RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "http://other.com/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->headers.count("Authorization"), 0u);
}

TEST(RedirectPolicyTest, SameOriginKeepsHeaders) {
    RequestInfo req;
    req.url = "http://example.com/old";
    req.headers["Authorization"] = "Bearer token";
    auto result =
        RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "http://example.com/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->headers["Authorization"], "Bearer token");
}

TEST(RedirectPolicyTest, BuildRedirectWithLocationOnly) {
    RequestInfo req;
    req.url = "http://example.com/old";
    auto result = RedirectPolicy::buildRedirectRequest(req, "/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, "http://example.com/new");
}

TEST(RedirectPolicyTest, ResolveRelativePath) {
    RequestInfo req;
    req.url = "http://example.com/path/old";
    auto result = RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, "http://example.com/new");
}

TEST(RedirectPolicyTest, ResolveWithPort) {
    RequestInfo req;
    req.url = "http://example.com:8080/old";
    auto result = RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, "http://example.com:8080/new");
}

TEST(RedirectPolicyTest, ResolveProtocolRelative) {
    RequestInfo req;
    req.url = "https://example.com/old";
    auto result =
        RedirectPolicy::buildRedirectRequest(req, HttpStatusCode::Found, "//other.com/new");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, "https://other.com/new");
}
