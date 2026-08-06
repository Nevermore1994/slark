
#include <gtest/gtest.h>

#include <memory>
#include <string>

using jobject = void*;
using jclass = void*;
using jstring = void*;
using jmethodID = void*;
using jfieldID = void*;
using JavaVM = void;

struct JNIEnv {
    void* functions;
};


TEST(JNIMockTest, ClassNameToPath) {
    std::string input = "com.example.Foo";
    for (auto& c : input) {
        if (c == '.') {
            c = '/';
        }
    }
    EXPECT_EQ(input, "com/example/Foo");
}

TEST(JNIMockTest, PathToClassName) {
    std::string input = "com/example/Foo";
    for (auto& c : input) {
        if (c == '/') {
            c = '.';
        }
    }
    EXPECT_EQ(input, "com.example.Foo");
}

TEST(JNIMockTest, ParseMethodSignature) {
    std::string sig = "(II)V";
    EXPECT_EQ(sig.front(), '(');
    EXPECT_EQ(sig.back(), 'V');
    auto params = sig.substr(1, sig.find(')') - 1);
    EXPECT_EQ(params, "II");
}

TEST(JNIMockTest, ReferenceCounting) {
    int refCount = 0;
    auto newRef = [&]() {
        refCount++;
        return reinterpret_cast<jobject>(static_cast<uintptr_t>(0x42));
    };
    auto deleteRef = [&](jobject) {
        refCount--;
    };

    auto ref1 = newRef();
    EXPECT_EQ(refCount, 1);
    auto ref2 = newRef();
    EXPECT_EQ(refCount, 2);
    deleteRef(ref1);
    EXPECT_EQ(refCount, 1);
    deleteRef(ref2);
    EXPECT_EQ(refCount, 0);
}

TEST(JNIMockTest, StringConversion) {
    std::string utf8 = "Hello 世界";
    EXPECT_EQ(utf8.size(), 12u);  // 7 ASCII + 6 UTF-8 bytes for 2 Chinese chars
    EXPECT_EQ(utf8.substr(0, 5), "Hello");
}
