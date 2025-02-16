//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/CoTag.h>

// ReSharper disable CppMemberFunctionMayBeStatic

namespace oxygen::co::detail {

template <class T>
struct RemoveRvalueReference {
    using type = T;
};
template <class T>
struct RemoveRvalueReference<T&> {
    using type = T&;
};
template <class T>
struct RemoveRvalueReference<T&&> {
    using type = T;
};
template <class T>
using RemoveRvalueReferenceT = typename RemoveRvalueReference<T>::type;

static_assert(std::is_same_v<RemoveRvalueReferenceT<const int&>, const int&>);
static_assert(std::is_same_v<RemoveRvalueReferenceT<int&&>, int>);

//! Mimics what the compiler does to obtain an awaiter from whatever
//! is passed to `co_await`, including a `lambda`.
/*!
 Any suitable `detail::operator co_await(T&&)` will be considered even if it
 would not be found via ADL, as long as the `operator co_await` is declared
 before `GetAwaiter()` is defined. You will need a corresponding
 `ThisIsAwaitableTrustMe` specialization in order to make the object satisfy
 `Awaitable`, since the `Awaitable` concept was declared before the `operator
 co_await`.

 The return type of this function is as follows:
 - If T&& is `ImmediateAwaitable`, then `T&&`. (Like std::forward: you get a
   lvalue or rvalue reference depending on the value category of `t`, and no
   additional object is created.)
 - If `T&&` defines `operator co_await()` returning value type `A` or rvalue
   reference `A&&`, then `A`. (The awaitable is constructed or moved into the
   return value slot.)
 - If `T&&` defines `operator co_await()` returning lvalue reference `A&`, then
   `A&`. (We do not make a copy.)

 It is important to pay attention to the value category in order to avoid a
 dangling reference if a function constructs a combination of awaiters and then
 returns it. Typically, the return value of `GetAwaiter(T&&)` should be used to
 initialize an object of type `AwaiterType<T&&>`; `AwaiterType` will be a `value
 type` or `lvalue reference`, but not a `rvalue reference`.
*/
template <class T>
auto GetAwaiter(T&& t) -> decltype(auto);

//! Returns the type that `GetAwaiter()` would return, stripped of any
//! rvalue-reference part (so you might get `T` or `T&`, but not `T&&`). This is
//! the appropriate type to store in an object that wraps other awaiters.
template <class Aw>
using AwaiterType = RemoveRvalueReferenceT<decltype(GetAwaiter(std::declval<Aw>()))>;

//! A quality-of-life adapter allowing passing lambdas returning `Co<>` instead
//! of tasks themselves, saving on a bunch of parentheses, and (most
//! importantly) not exposing users to problems with lifetimes of lambda
//! objects themselves.
template <class Callable>
class AwaitableLambda {
    using TaskT = std::invoke_result_t<Callable>;
    using AwaiterT = AwaiterType<TaskT>;

public:
    // ReSharper disable once CppPossiblyUninitializedMember - lazy init when appropriate
    explicit AwaitableLambda(Callable&& c) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        : callable_(std::forward<Callable>(c))
    {
    }

    ~AwaitableLambda()
    {
        if (task_.IsValid()) {
            awaiter_.~AwaiterT();
        }
    }

    OXYGEN_MAKE_NON_MOVABLE(AwaitableLambda)
    OXYGEN_DEFAULT_COPYABLE(AwaitableLambda)

    // NB: these forwarders are specialized for `TaskAwaiter`, and would need
    // generalization to support non-Task awaitables

    // We know that a `TaskAwaiter` will be not-ready (except `NoOp()`, but
    // that one doesn't mind if you suspend on it anyway). We need to initialize
    // the awaitable before `await_resume()` gets called, can't do it here since
    // the method is const, and `await_set_executor()` only runs if we're going
    // to suspend.
    [[nodiscard]] auto await_ready() const noexcept { return false; }

    void await_set_executor(Executor* ex) noexcept
    {
        GetAwaiter().await_set_executor(ex);
    }

    auto await_suspend(Handle h) { return awaiter_.await_suspend(h); }

    auto await_resume() -> decltype(auto)
    {
        return std::forward<AwaiterT>(awaiter_).await_resume();
    }

    auto await_early_cancel() noexcept
    {
        return GetAwaiter().await_early_cancel();
    }

    auto await_cancel(Handle h) noexcept { return awaiter_.await_cancel(h); }

    [[nodiscard]] auto await_must_resume() const noexcept
    {
        return awaiter_.await_must_resume();
    }

private:
    auto GetAwaiter() -> AwaiterT&
    {
        if (!task_.IsValid()) {
            task_ = callable_();
            static_assert(noexcept(AwaiterT(task_.operator co_await())));
            new (&awaiter_) AwaiterT(task_.operator co_await());
        }

        return awaiter_;
    }

    Callable callable_;
    TaskT task_;
    union {
        AwaiterT awaiter_;
    };
};

template <class Callable>
    requires(std::derived_from<std::invoke_result_t<Callable>, CoTag>)
auto operator co_await(Callable&& c)
{
    return AwaitableLambda<Callable>(std::forward<Callable>(c));
}

// The AwaitableLambda `operator co_await()` is not found via ADL and was not
// declared before `concept Awaitable`, so we need to specialize
// `ThisIsAwaitableTrustMe` in order to make callables returning Co<T> satisfy
// `Awaitable`.
//
// Note that compiler-generated `co_await` logic outside `namespace co::detail`
// would similarly not find it, but since our `BasePromise::await_transform()`
// uses `co::detail::GetAwaiter()`, tasks can await lambdas. We specifically
// _don't_ want to enable this for foreign tasks, because they won't know to
// call `await_set_executor`, which prevents AwaitableLambda from working.
template <class Callable, class Ret>
    requires(std::derived_from<std::invoke_result_t<Callable>, CoTag>
                && (std::same_as<Ret, Unspecified>
                    || std::convertible_to<
                        typename std::invoke_result_t<Callable>::ReturnType,
                        Ret>))
constexpr bool kThisIsAwaitableTrustMe<Callable, Ret> = true;

//! A utility awaitable to perform a function with the current task temporarily
//! suspended. Can be used to add a suspension point.
template <class Callable, class ReturnType>
class [[nodiscard]] YieldToRunImpl : /*private*/ Callable {
public:
    explicit YieldToRunImpl(Callable cb)
        : Callable(std::move(cb))
    {
    }

    [[nodiscard]] auto await_ready() const noexcept { return false; }
    [[nodiscard]] auto await_suspend([[maybe_unused]] Handle h)
    {
        result_ = Callable::operator()();
        return false;
    }
    auto await_resume() -> ReturnType { return std::move(result_); }
    void await_introspect(auto& c) const noexcept { c.node("Yield"); }

private:
    ReturnType result_;
};

template <class Callable, class ReturnType>
class [[nodiscard]] YieldToRunImpl<Callable, ReturnType&&> : /*private*/ Callable {
public:
    explicit YieldToRunImpl(Callable cb)
        : Callable(std::move(cb))
    {
    }

    [[nodiscard]] auto await_ready() const noexcept { return false; }
    [[nodiscard]] auto await_suspend([[maybe_unused]] Handle h)
    {
        result_ = &Callable::operator()();
        return false;
    }
    auto await_resume() -> ReturnType&& { return static_cast<ReturnType&&>(result_); }
    void await_introspect(auto& c) const noexcept { c.node("Yield"); }

private:
    ReturnType* result_ = nullptr;
};

template <class Callable>
class [[nodiscard]] YieldToRunImpl<Callable, void> : /*private*/ Callable {
public:
    explicit YieldToRunImpl(Callable cb)
        : Callable(std::move(cb))
    {
    }

    [[nodiscard]] auto await_ready() const noexcept { return false; }
    [[nodiscard]] auto await_suspend([[maybe_unused]] Handle h)
    {
        Callable::operator()();
        return false;
    }
    void await_resume() { }
    void await_introspect(auto& c) const noexcept { c.node("Yield"); }
};

template <class Callable>
using YieldToRunAwaitable = YieldToRunImpl<Callable, std::invoke_result_t<Callable>>;

template <ValidAwaiter Aw>
consteval void StaticAwaiterCheck() { }

template <class T>
auto GetAwaiter(T&& t) -> decltype(auto)
{
    static_assert(oxygen::co::Awaitable<T>, "tried to co_await on not an awaitable");

    if constexpr (ImmediateAwaitable<T>) {
        StaticAwaiterCheck<T>();
        return std::forward<T>(t);
    } else if constexpr (MemberCoAwaitAwaitable<T>) {
        using Ret = decltype(std::forward<T>(t).operator co_await());
        StaticAwaiterCheck<Ret>();
        return std::forward<T>(t).operator co_await();
    } else if constexpr (
        // The direct `requires()` expression here rather than using the
        // pre-defined concept `NonMemberCoAwaitAwaitable` serves two purposes:
        // 1. It properly handles forwarding references through
        //    std::forward<T>(t)
        // 2. It performs ADL (Argument Dependent Lookup) for the operator
        //    `co_await`
        //
        // This concept definition would need to be evaluated at the point where
        // it's defined, while the current implementation in GetAwaiter allows
        // for operators that are defined later.
        requires() {
            { operator co_await(std::forward<T>(t)) } -> ImmediateAwaitable;
        }) {
        using Ret = decltype(operator co_await(std::forward<T>(t)));
        StaticAwaiterCheck<Ret>();
        return operator co_await(std::forward<T>(t));
    } else {
        // if !Awaitable<T>, then the static_assert above fired, and we don't
        // need to fire this one also
        static_assert(!Awaitable<T>,
            "co_await argument satisfies Awaitable concept but "
            "we couldn't extract a `ImmediateAwaitable` from it");
        return std::suspend_never {};
    }
}

} // namespace oxygen::co::detail
