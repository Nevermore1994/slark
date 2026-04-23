//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "JNIBase.h"
#include "JNICache.h"
#include "JNIMarshal.h"
#include "JNISignature.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace slark::JNI {

// 运行时签名的 jobject 参数包装器
// 用于参数类型为具体 Java 类时，携带精确签名
// 可选持有引用的所有权（RAII），避免 .detach() 导致的 local ref 泄漏
struct JObjectWithSig {
    jobject obj;
    std::string_view sig;

    JObjectWithSig(jobject o, std::string_view s) noexcept : obj(o), sig(s) {}

    // 从 StringReference 构造，持有其所有权（RAII）
    JObjectWithSig(StringReference ref, std::string_view s) noexcept
        : obj(ref.get()), sig(s), ownedStrRef_(std::move(ref)) {}

    // 从 ObjectReference 构造，持有其所有权（RAII）
    JObjectWithSig(ObjectReference ref, std::string_view s) noexcept
        : obj(ref.get()), sig(s), ownedObjRef_(std::move(ref)) {}

    explicit operator bool() const noexcept {
        return obj != nullptr;
    }

private:
    StringReference ownedStrRef_;
    ObjectReference ownedObjRef_;
};

// 运行时返回值签名
// 用于返回值为具体 Java 类时，指定精确签名
struct ReturnSig {
    std::string_view sig;

    explicit ReturnSig(std::string_view s) noexcept : sig(s) {}
};

// 编译期签名推导
template <typename T>
constexpr std::string_view sigOf() {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, void>) {
        return Sig::Void;
    } else if constexpr (std::is_same_v<D, bool> || std::is_same_v<D, jboolean>) {
        return Sig::Bool;
    } else if constexpr (std::is_same_v<D, int32_t> || std::is_same_v<D, jint>) {
        return Sig::Int;
    } else if constexpr (std::is_same_v<D, int64_t> || std::is_same_v<D, jlong>) {
        return Sig::Long;
    } else if constexpr (std::is_same_v<D, float> || std::is_same_v<D, jfloat>) {
        return Sig::Float;
    } else if constexpr (std::is_same_v<D, double> || std::is_same_v<D, jdouble>) {
        return Sig::Double;
    } else if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>) {
        return Sig::String;
    } else if constexpr (std::is_same_v<D, jbyteArray>) {
        return Sig::ByteArray;
    } else if constexpr (std::is_same_v<D, jintArray>) {
        return Sig::IntArray;
    } else if constexpr (std::is_same_v<D, jstring>) {
        return Sig::String;
    } else if constexpr (std::is_same_v<D, JObjectWithSig>) {
        // 运行时签名，无法在编译期返回，此分支不应被 methodSig() 调用
        // 实际签名由 methodSigRuntime() 处理
        return "Ljava/lang/Object;";
    } else {
        static_assert(!sizeof(D), "sigOf<T>: unknown type");
        return "";
    }
}

template <typename... Args>
std::string methodSig() {
    return makeSignature(sigOf<Args>()...);
}

// 从参数列表中提取每个参数的签名（支持 JObjectWithSig 的运行时签名）
template <typename T>
std::string sigOfArg(const T& v) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, JObjectWithSig>) {
        return std::string(v.sig);
    } else {
        return std::string(sigOf<D>());
    }
}

template <typename R, typename... Args>
std::string methodSigRuntime(const Args&... args) {
    return makeSignature(sigOf<R>(), sigOfArg(args)...);
}

// 参数转换：基础类型直接转，string 类型返回 StringReference（RAII 管理 local ref）
template <typename T>
auto toArg(JNIEnv* env, T&& v) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return jboolean(v ? JNI_TRUE : JNI_FALSE);
    } else if constexpr (std::is_same_v<D, int32_t>) {
        return jint(v);
    } else if constexpr (std::is_same_v<D, int64_t>) {
        return jlong(v);
    } else if constexpr (std::is_same_v<D, float>) {
        return jfloat(v);
    } else if constexpr (std::is_same_v<D, double>) {
        return jdouble(v);
    } else if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>) {
        return ToJVM::str(env, v);
    } else if constexpr (std::is_same_v<D, JObjectWithSig>) {
        return v.obj;
    } else {
        return std::forward<T>(v);
    }
}

// 从转换结果中提取实际传给 JNI 的值（StringReference 需要 .get()）
template <typename T>
auto argValue(T&& v) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, StringReference>) {
        return v.get();
    } else {
        return std::forward<T>(v);
    }
}

class JNICall {
public:
    // 调用静态方法
    // 参数中若含有 JObjectWithSig，会自动使用其携带的精确签名
    template <typename R, typename... Args>
    static auto callStatic(JNIEnv* env,
                           std::string_view cls,
                           std::string_view name,
                           Args&&... args) {
        auto c = JNICache::instance().getClass(env, cls);
        if (!c) {
            return emptyResult<R>();
        }
        constexpr bool hasRuntimeSig = (std::is_same_v<std::decay_t<Args>, JObjectWithSig> || ...);
        std::string sig;
        if constexpr (hasRuntimeSig) {
            sig = makeSignature(sigOf<R>(), sigOfArg(args)...);
        } else {
            sig = methodSig<R, Args...>();
        }
        auto m = JNICache::instance().getStaticMethod(c, name, sig);
        return m ? invokeStatic<R>(env, c.get(), m.get(), std::forward<Args>(args)...)
                 : emptyResult<R>();
    }

    // 调用静态方法（指定精确返回类型签名）
    // 返回值为具体 Java 类时必须传入 ReturnSig，参数中若含有 JObjectWithSig 同样兼容
    template <typename R, typename... Args>
    static auto callStatic(JNIEnv* env,
                           std::string_view cls,
                           std::string_view name,
                           ReturnSig returnSig,
                           Args&&... args) {
        auto c = JNICache::instance().getClass(env, cls);
        if (!c) {
            return emptyResult<R>();
        }
        auto sig = makeSignature(returnSig.sig, sigOfArg(args)...);
        auto m = JNICache::instance().getStaticMethod(c, name, sig);
        return m ? invokeStatic<R>(env, c.get(), m.get(), std::forward<Args>(args)...)
                 : emptyResult<R>();
    }

    // 调用实例方法
    // 参数中若含有 JObjectWithSig，会自动使用其携带的精确签名
    template <typename R, typename... Args>
    static auto call(JNIEnv* env,
                     jobject obj,
                     std::string_view cls,
                     std::string_view name,
                     Args&&... args) {
        auto c = JNICache::instance().getClass(env, cls);
        if (!c) {
            return emptyResult<R>();
        }
        constexpr bool hasRuntimeSig = (std::is_same_v<std::decay_t<Args>, JObjectWithSig> || ...);
        std::string sig;
        if constexpr (hasRuntimeSig) {
            sig = makeSignature(sigOf<R>(), sigOfArg(args)...);
        } else {
            sig = methodSig<R, Args...>();
        }
        auto m = JNICache::instance().getMethod(c, name, sig);
        return m ? invoke<R>(env, obj, m.get(), std::forward<Args>(args)...) : emptyResult<R>();
    }

    // 调用实例方法（指定精确返回类型签名）
    // 返回值为具体 Java 类时必须传入 ReturnSig，参数中若含有 JObjectWithSig 同样兼容
    template <typename R, typename... Args>
    static auto call(JNIEnv* env,
                     jobject obj,
                     std::string_view cls,
                     std::string_view name,
                     ReturnSig returnSig,
                     Args&&... args) {
        auto c = JNICache::instance().getClass(env, cls);
        if (!c) {
            return emptyResult<R>();
        }
        auto sig = makeSignature(returnSig.sig, sigOfArg(args)...);
        auto m = JNICache::instance().getMethod(c, name, sig);
        return m ? invoke<R>(env, obj, m.get(), std::forward<Args>(args)...) : emptyResult<R>();
    }

    template <typename... Args>
    static ObjectReference create(JNIEnv* env, std::string_view cls, Args&&... args) {
        auto c = JNICache::instance().getClass(env, cls);
        if (!c) {
            return {env, nullptr};
        }
        auto m = JNICache::instance().getMethod(c, "<init>", makeSignature("V", sigOf<Args>()...));
        if (!m) {
            return {env, nullptr};
        }
        return {
            env,
            env->NewObject(c.get(), m.get(), argValue(toArg(env, std::forward<Args>(args)))...)};
    }

    // 获取静态字段（基础类型返回 optional<R>，jobject / jbyteArray 等对象类型返回 ObjectReference）
    // sig：字段的 JNI 类型签名，对象类型必须传入真实签名（如 "[B"），基础类型可省略
    template <typename R>
    static auto getStaticField(JNIEnv* env,
                               std::string_view cls,
                               std::string_view name,
                               std::string_view sig = "") {
        auto c = JNICache::instance().getClass(env, cls);
        if constexpr (std::is_same_v<R, jobject>) {
            if (!c) {
                return ObjectReference(env, nullptr);
            }
            const auto fieldSig = sig.empty() ? sigOf<R>() : sig;
            auto f = env->GetStaticFieldID(c.get(),
                                           std::string(name).c_str(),
                                           std::string(fieldSig).c_str());
            if (!f || env->ExceptionCheck()) {
                return ObjectReference(env, nullptr);
            }
            return ObjectReference(env, env->GetStaticObjectField(c.get(), f));
        } else {
            if (!c) {
                return std::optional<R>(std::nullopt);
            }
            auto f = env->GetStaticFieldID(c.get(),
                                           std::string(name).c_str(),
                                           std::string(sigOf<R>()).c_str());
            if (!f || env->ExceptionCheck()) {
                return std::optional<R>(std::nullopt);
            }
            return readField<R, true>(env, c.get(), f);
        }
    }

    // 获取实例字段（基础类型返回 optional<R>，jobject / jbyteArray 等对象类型返回 ObjectReference）
    // sig：字段的 JNI 类型签名，对象类型必须传入真实签名（如 "[B"），基础类型可省略
    template <typename R>
    static auto getField(JNIEnv* env,
                         jobject obj,
                         std::string_view cls,
                         std::string_view name,
                         std::string_view sig = "") {
        auto c = JNICache::instance().getClass(env, cls);
        if constexpr (std::is_same_v<R, jobject>) {
            if (!c) {
                return ObjectReference(env, nullptr);
            }
            const auto fieldSig = sig.empty() ? sigOf<R>() : sig;
            auto f =
                env->GetFieldID(c.get(), std::string(name).c_str(), std::string(fieldSig).c_str());
            if (!f || env->ExceptionCheck()) {
                return ObjectReference(env, nullptr);
            }
            return ObjectReference(env, env->GetObjectField(obj, f));
        } else {
            if (!c) {
                return std::optional<R>(std::nullopt);
            }
            auto f = env->GetFieldID(c.get(),
                                     std::string(name).c_str(),
                                     std::string(sigOf<R>()).c_str());
            if (!f || env->ExceptionCheck()) {
                return std::optional<R>(std::nullopt);
            }
            return readField<R, false>(env, obj, f);
        }
    }

private:
    template <typename R>
    static auto emptyResult() {
        if constexpr (std::is_same_v<R, void>) {
            return false;
        } else if constexpr (std::is_same_v<R, jobject>) {
            // callStatic<jobject> / call<jobject> 返回 ObjectReference，emptyResult 需保持一致
            return ObjectReference(nullptr, nullptr);
        } else {
            return std::optional<R>{std::nullopt};
        }
    }

    template <typename R, typename... Args>
    static auto invokeStatic(JNIEnv* env, jclass cls, jmethodID m, Args&&... args) {
        if constexpr (std::is_same_v<R, void>) {
            env->CallStaticVoidMethod(cls, m, argValue(toArg(env, std::forward<Args>(args)))...);
            return !env->ExceptionCheck();
        } else if constexpr (std::is_same_v<R, jobject>) {
            auto r = env->CallStaticObjectMethod(cls,
                                                 m,
                                                 argValue(toArg(env, std::forward<Args>(args)))...);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return ObjectReference(env, nullptr);
            }
            return ObjectReference(env, r);
        } else {
            auto r = [&] {
                if constexpr (std::is_same_v<R, int32_t>) {
                    return env->CallStaticIntMethod(
                        cls,
                        m,
                        argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, int64_t>) {
                    return env->CallStaticLongMethod(
                        cls,
                        m,
                        argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, bool>) {
                    return env->CallStaticBooleanMethod(
                        cls,
                        m,
                        argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, float>) {
                    return env->CallStaticFloatMethod(
                        cls,
                        m,
                        argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, double>) {
                    return env->CallStaticDoubleMethod(
                        cls,
                        m,
                        argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, std::string>) {
                    StringReference jstr(env,
                                         (jstring)env->CallStaticObjectMethod(
                                             cls,
                                             m,
                                             argValue(toArg(env, std::forward<Args>(args)))...));
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        return std::string{};
                    }
                    return jstr ? FromJVM::str(env, jstr.get()) : std::string{};
                } else {
                    static_assert(!sizeof(R), "unsupported return type for callStatic");
                }
            }();
            return env->ExceptionCheck() ? std::optional<R>(std::nullopt) : std::optional<R>(r);
        }
    }

    template <typename R, typename... Args>
    static auto invoke(JNIEnv* env, jobject obj, jmethodID m, Args&&... args) {
        if constexpr (std::is_same_v<R, void>) {
            env->CallVoidMethod(obj, m, argValue(toArg(env, std::forward<Args>(args)))...);
            return !env->ExceptionCheck();
        } else if constexpr (std::is_same_v<R, jobject>) {
            auto r =
                env->CallObjectMethod(obj, m, argValue(toArg(env, std::forward<Args>(args)))...);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return ObjectReference(env, nullptr);
            }
            return ObjectReference(env, r);
        } else {
            auto r = [&] {
                if constexpr (std::is_same_v<R, int32_t>) {
                    return env->CallIntMethod(obj,
                                              m,
                                              argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, int64_t>) {
                    return env->CallLongMethod(obj,
                                               m,
                                               argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, bool>) {
                    return env->CallBooleanMethod(
                        obj,
                        m,
                        argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, float>) {
                    return env->CallFloatMethod(obj,
                                                m,
                                                argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, double>) {
                    return env->CallDoubleMethod(obj,
                                                 m,
                                                 argValue(toArg(env, std::forward<Args>(args)))...);
                } else if constexpr (std::is_same_v<R, std::string>) {
                    StringReference jstr(env,
                                         (jstring)env->CallObjectMethod(
                                             obj,
                                             m,
                                             argValue(toArg(env, std::forward<Args>(args)))...));
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        return std::string{};
                    }
                    return jstr ? FromJVM::str(env, jstr.get()) : std::string{};
                } else {
                    static_assert(!sizeof(R), "unsupported return type for call");
                }
            }();
            return env->ExceptionCheck() ? std::optional<R>(std::nullopt) : std::optional<R>(r);
        }
    }

    template <typename R, bool Static, typename ObjT>
    static std::optional<R> readField(JNIEnv* env, ObjT obj, jfieldID f) {
        if constexpr (Static) {
            if constexpr (std::is_same_v<R, int32_t>) {
                return env->GetStaticIntField(obj, f);
            } else if constexpr (std::is_same_v<R, int64_t>) {
                return env->GetStaticLongField(obj, f);
            } else if constexpr (std::is_same_v<R, bool>) {
                return env->GetStaticBooleanField(obj, f) == JNI_TRUE;
            } else if constexpr (std::is_same_v<R, float>) {
                return env->GetStaticFloatField(obj, f);
            } else if constexpr (std::is_same_v<R, double>) {
                return env->GetStaticDoubleField(obj, f);
            } else if constexpr (std::is_same_v<R, std::string>) {
                StringReference jstr(env, (jstring)env->GetStaticObjectField(obj, f));
                if (!jstr) {
                    return std::nullopt;
                }
                return FromJVM::str(env, jstr.get());
            } else {
                return std::nullopt;
            }
        } else {
            if constexpr (std::is_same_v<R, int32_t>) {
                return env->GetIntField(obj, f);
            } else if constexpr (std::is_same_v<R, int64_t>) {
                return env->GetLongField(obj, f);
            } else if constexpr (std::is_same_v<R, bool>) {
                return env->GetBooleanField(obj, f) == JNI_TRUE;
            } else if constexpr (std::is_same_v<R, float>) {
                return env->GetFloatField(obj, f);
            } else if constexpr (std::is_same_v<R, double>) {
                return env->GetDoubleField(obj, f);
            } else if constexpr (std::is_same_v<R, std::string>) {
                StringReference jstr(env, (jstring)env->GetObjectField(obj, f));
                if (!jstr) {
                    return std::nullopt;
                }
                return FromJVM::str(env, jstr.get());
            } else {
                return std::nullopt;
            }
        }
    }
};

} // namespace slark::JNI
