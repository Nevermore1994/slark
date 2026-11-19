//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include "UrlPath.hpp"

TEST(UrlPathTest, RemovesDotSegmentsPerRfc3986) {
    EXPECT_EQ(slark::removeDotSegments("/a/b/c/./../../g"), "/a/g");
    EXPECT_EQ(slark::removeDotSegments("mid/content=5/../6"), "mid/6");
    EXPECT_EQ(slark::removeDotSegments("/a/b/../.."), "/");
}

TEST(UrlPathTest, PreservesTrailingSlashAfterParentSegment) {
    EXPECT_EQ(slark::removeDotSegments("/a/b/.."), "/a/");
    EXPECT_EQ(slark::removeDotSegments("/a/b/../"), "/a/");
    EXPECT_EQ(slark::removeDotSegments("/a/b/g/"), "/a/b/g/");
}

TEST(UrlPathTest, HandlesEmptyAndBareDotSegments) {
    EXPECT_EQ(slark::removeDotSegments(""), "");
    EXPECT_EQ(slark::removeDotSegments("."), "");
    EXPECT_EQ(slark::removeDotSegments(".."), "");
    EXPECT_EQ(slark::removeDotSegments("/."), "/");
    EXPECT_EQ(slark::removeDotSegments("/.."), "/");
}

TEST(UrlPathTest, PreservesEmptySegments) {
    EXPECT_EQ(slark::removeDotSegments("/a//b"), "/a//b");
    EXPECT_EQ(slark::removeDotSegments("//a/../b"), "//b");
}
