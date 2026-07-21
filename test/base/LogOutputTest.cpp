#include "LogOutput.h"
#include <gtest/gtest.h>

using namespace slark;

TEST(LogOutputTest, Singleton) {
    auto& lo1 = LogOutput::shareInstance();
    auto& lo2 = LogOutput::shareInstance();
    EXPECT_EQ(&lo1, &lo2);
}

TEST(LogOutputTest, WriteDoesNotCrash) {
    auto& lo = LogOutput::shareInstance();
    EXPECT_NO_THROW(lo.write("test message"));
}
