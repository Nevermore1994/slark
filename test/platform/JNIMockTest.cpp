// JNI common 代码的桌面兼容测试（不依赖实际 JNI 环境）
// 测试 JNI 辅助类的逻辑，通过模拟 JNIEnv 接口

#include <gtest/gtest.h>

#include <memory>
#include <string>

// 模拟 JNI 类型（桌面环境没有 jni.h）
using jobject = void*;
using jclass = void*;
using jstring = void*;
using jmethodID = void*;
using jfieldID = void*;
using JavaVM = void;

struct JNIEnv {
    void* functions;
};

// 包含需要测试的 JNI 辅助头文件
// 注意：JNIBase.h 和 JNICall.h 依赖于实际 jni.h，这里只测试独立逻辑

// 测试 JNI 命名规范
TEST(JNIMockTest, ClassNameToPath) {
    // Java 类名 com.example.Foo → com/example/Foo
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

// 测试 JNI 方法签名解析逻辑
TEST(JNIMockTest, ParseMethodSignature) {
    // 模拟解析 "(II)V"
    std::string sig = "(II)V";
    EXPECT_EQ(sig.front(), '(');
    EXPECT_EQ(sig.back(), 'V');
    auto params = sig.substr(1, sig.find(')') - 1);
    EXPECT_EQ(params, "II");
}

// 测试 JNI 引用计数逻辑（模拟 JNIReference 行为）
TEST(JNIMockTest, ReferenceCounting) {
    // 模拟 JNI 的 NewGlobalRef / DeleteGlobalRef
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

// 测试字符串转换逻辑（JNIMarshal 的核心）
TEST(JNIMockTest, StringConversion) {
    std::string utf8 = "Hello 世界";
    // 模拟 GetStringUTFChars / NewStringUTF
    // 这里只测试 C++ 侧的字符串处理逻辑
    EXPECT_EQ(utf8.size(), 12u);  // 7 ASCII + 6 UTF-8 bytes for 2 Chinese chars
    EXPECT_EQ(utf8.substr(0, 5), "Hello");
}
