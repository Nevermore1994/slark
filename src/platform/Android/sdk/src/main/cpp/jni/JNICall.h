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

    // 从 ByteArrayReference 构造，持有其所有权（RAII）
    JObjectWithSig(ByteArrayReference ref, std::string_view s) noexcept
        : obj(reinterpret_cast<jobject>(ref.get())), sig(s), ownedByteArrayRef_(std::move(ref)) {}

    explicit operator bool() const noexcept {
        return obj != nullptr;
    }

private:
    StringReference ownedStrRef_;
    ObjectReference ownedObjRef_;
    ByteArrayReference ownedByteArrayRef_;
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
    } else if constexpr (std::is_same_v<D, jobject>) {
        return "Ljava/lang/Object;";
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

    // 构造 Java 对象（支持 JObjectWithSig 运行时签名）
    template <typename... Args>
    static ObjectReference create(JNIEnv* env, std::string_view cls, Args&&... args) {
        auto c = JNICache::instance().getClass(env, cls);
        if (!c) {
            return {env, nullptr};
        }
        // 检查是否含有 JObjectWithSig 参数（需要运行时签名）
        constexpr bool hasRuntimeSig = (std::is_same_v<std::decay_t<Args>, JObjectWithSig> || ...);
        std::string sig;
        if constexpr (hasRuntimeSig) {
            // 使用参数的运行时签名，构造函数返回值为 V
            sig = std::string("(");
            ((sig.append(sigOfArg(args))), ...);
            sig.append(")V");
        } else {
            sig = makeSignature("V", sigOf<Args>()...);
        }
        auto m = JNICache::instance().getMethod(c, "<init>", sig);
        if (!m) {
            return {env, nullptr};
        }
        return {
            env,
            env->NewObject(c.get(), m.get(), argValue(toArg(env, std::forward<Args>(args)))...)};
    }

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
        } else if constexpr (std::is_same_v<R, std::string>) {
            return std::string{};
        } else {
            return std::optional<R>{std::nullopt};
        }
    }

    // 基础类型分发：接受一个 lambda 提供实际 JNI 调用，消除 invokeStatic/invoke 的重复分支
    template <typename R, typename CallFn>
    static auto dispatchPrimitive(JNIEnv* env, CallFn&& callFn) {
        if constexpr (std::is_same_v<R, std::string>) {
            StringReference jstr(env, static_cast<jstring>(callFn()));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return std::string{};
            }
            return jstr ? FromJVM::str(env, jstr.get()) : std::string{};
        } else {
            auto r = callFn();
            if constexpr (std::is_same_v<R, bool>) {
                return env->ExceptionCheck() ? std::optional<R>(std::nullopt)
                                             : std::optional<R>(r != 0);
            } else {
                return env->ExceptionCheck() ? std::optional<R>(std::nullopt)
                                             : std::optional<R>(static_cast<R>(r));
            }
        }
    }

    // 统一调用分发：voidFn/objectFn/primitiveFn 分别对应三种返回类型
    // 调用方只需传入对应的 lambda，不再重复 if constexpr 链
    template <typename R, typename VoidFn, typename ObjectFn, typename PrimFn>
    static auto dispatchCall(JNIEnv* env, VoidFn&& voidFn, ObjectFn&& objectFn, PrimFn&& primFn) {
        if constexpr (std::is_same_v<R, void>) {
            voidFn();
            return !env->ExceptionCheck();
        } else if constexpr (std::is_same_v<R, jobject>) {
            auto r = objectFn();
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return ObjectReference(env, nullptr);
            }
            return ObjectReference(env, r);
        } else if constexpr (std::is_same_v<R, int32_t>) {
            return dispatchPrimitive<R>(env, [&] {
                return primFn.fn.template operator()<jint>();
            });
        } else if constexpr (std::is_same_v<R, int64_t>) {
            return dispatchPrimitive<R>(env, [&] {
                return primFn.fn.template operator()<jlong>();
            });
        } else if constexpr (std::is_same_v<R, bool>) {
            return dispatchPrimitive<R>(env, [&] {
                return primFn.fn.template operator()<jboolean>();
            });
        } else if constexpr (std::is_same_v<R, float>) {
            return dispatchPrimitive<R>(env, [&] {
                return primFn.fn.template operator()<jfloat>();
            });
        } else if constexpr (std::is_same_v<R, double>) {
            return dispatchPrimitive<R>(env, [&] {
                return primFn.fn.template operator()<jdouble>();
            });
        } else if constexpr (std::is_same_v<R, std::string>) {
            return dispatchPrimitive<R>(env, [&] {
                return primFn.fn.template operator()<jobject>();
            });
        } else {
            static_assert(!sizeof(R), "unsupported return type");
        }
    }

    // invokeImpl<R, IsStatic>：static/instance 两路合一，零开销，消除两套 PrimCaller
    template <typename R, bool IsStatic, typename ReceiverT, typename... JArgs>
    static auto invokeImpl(JNIEnv* env, ReceiverT receiver, jmethodID m, JArgs... jargs) {
        auto callVoid = [&] {
            if constexpr (IsStatic) {
                env->CallStaticVoidMethod(receiver, m, jargs...);
            } else {
                env->CallVoidMethod(receiver, m, jargs...);
            }
        };
        auto callObject = [&] -> jobject {
            if constexpr (IsStatic) {
                return env->CallStaticObjectMethod(receiver, m, jargs...);
            } else {
                return env->CallObjectMethod(receiver, m, jargs...);
            }
        };
        // 基础类型分发 lambda，T 决定具体 JNI 函数
        auto callPrim = [&]<typename T>() {
            if constexpr (IsStatic) {
                if constexpr (std::is_same_v<T, jint>) {
                    return env->CallStaticIntMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jlong>) {
                    return env->CallStaticLongMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jboolean>) {
                    return env->CallStaticBooleanMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jfloat>) {
                    return env->CallStaticFloatMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jdouble>) {
                    return env->CallStaticDoubleMethod(receiver, m, jargs...);
                } else {
                    return env->CallStaticObjectMethod(receiver, m, jargs...);
                }
            } else {
                if constexpr (std::is_same_v<T, jint>) {
                    return env->CallIntMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jlong>) {
                    return env->CallLongMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jboolean>) {
                    return env->CallBooleanMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jfloat>) {
                    return env->CallFloatMethod(receiver, m, jargs...);
                } else if constexpr (std::is_same_v<T, jdouble>) {
                    return env->CallDoubleMethod(receiver, m, jargs...);
                } else {
                    return env->CallObjectMethod(receiver, m, jargs...);
                }
            }
        };

        struct PrimDispatcher {
            decltype(callPrim)& fn;
        } prim{callPrim};

        return dispatchCall<R>(env, callVoid, callObject, prim);
    }

    template <typename R, typename... Args>
    static auto invokeStatic(JNIEnv* env, jclass cls, jmethodID m, Args&&... args) {
        auto jargs = std::make_tuple(toArg(env, std::forward<Args>(args))...);
        return std::apply(
            [&](auto&&... a) {
                return invokeImpl<R, true>(env, cls, m, argValue(a)...);
            },
            jargs);
    }

    template <typename R, typename... Args>
    static auto invoke(JNIEnv* env, jobject obj, jmethodID m, Args&&... args) {
        auto jargs = std::make_tuple(toArg(env, std::forward<Args>(args))...);
        return std::apply(
            [&](auto&&... a) {
                return invokeImpl<R, false>(env, obj, m, argValue(a)...);
            },
            jargs);
    }

    // 字段读取：接受 getter lambda，消除 Static/instance 两套重复分支
    template <typename R, typename GetterFn>
    static std::optional<R> readFieldWith(JNIEnv* env, GetterFn&& getter) {
        if constexpr (std::is_same_v<R, std::string>) {
            StringReference jstr(env, static_cast<jstring>(getter()));
            return jstr ? std::optional<R>(FromJVM::str(env, jstr.get())) : std::nullopt;
        } else if constexpr (std::is_same_v<R, bool>) {
            return static_cast<jboolean>(getter()) == JNI_TRUE;
        } else {
            return static_cast<R>(getter());
        }
    }

    // readField<R, IsStatic>：static/instance 合一，类型分发集中在一处
    template <typename R, bool IsStatic, typename ObjT>
    static std::optional<R> readField(JNIEnv* env, ObjT obj, jfieldID f) {
        if constexpr (std::is_same_v<R, int32_t>) {
            return readFieldWith<R>(env, [&] {
                return IsStatic ? env->GetStaticIntField(obj, f) : env->GetIntField(obj, f);
            });
        } else if constexpr (std::is_same_v<R, int64_t>) {
            return readFieldWith<R>(env, [&] {
                return IsStatic ? env->GetStaticLongField(obj, f) : env->GetLongField(obj, f);
            });
        } else if constexpr (std::is_same_v<R, bool>) {
            return readFieldWith<R>(env, [&] {
                return IsStatic ? env->GetStaticBooleanField(obj, f) : env->GetBooleanField(obj, f);
            });
        } else if constexpr (std::is_same_v<R, float>) {
            return readFieldWith<R>(env, [&] {
                return IsStatic ? env->GetStaticFloatField(obj, f) : env->GetFloatField(obj, f);
            });
        } else if constexpr (std::is_same_v<R, double>) {
            return readFieldWith<R>(env, [&] {
                return IsStatic ? env->GetStaticDoubleField(obj, f) : env->GetDoubleField(obj, f);
            });
        } else if constexpr (std::is_same_v<R, std::string>) {
            return readFieldWith<R>(env, [&] {
                return IsStatic ? env->GetStaticObjectField(obj, f) : env->GetObjectField(obj, f);
            });
        } else {
            return std::nullopt;
        }
    }
};

} // namespace slark::JNI
