//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include "Oxygen/OxCo/Concepts/Awaitable.h"
#include "Oxygen/OxCo/Concepts/Cancellation.h"
#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/CoTag.h"

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

//! Mimics what the compiler does to obtain an awaitable from whatever
//! is passed to co_await, plus a fallback to support AwaitableLambda.
/*!
 Any suitable `detail::operator co_await(T&&)` will be considered even if it
 would not be found via ADL, as long as the `operator co_await` is declared
 before `GetAwaitable()` is defined. You will need a corresponding
 `ThisIsAwaitableTrustMe` specialization in order to make the object satisfy
 `Awaitable`, since the `Awaitable` concept was declared before the `operator
 co_await`.

 The return type of this function is as follows:
 - If T&& is `DirectAwaitable`, then `T&&`. (Like std::forward: you get a lvalue
   or rvalue reference depending on the value category of `t`, and no additional
   object is created.)
 - If `T&&` defines `operator co_await()` returning value type `A` or rvalue
   reference `A&&`, then `A`. (The awaitable is constructed or moved into the
   return value slot.)
 - If `T&&` defines `operator co_await()` returning lvalue reference `A&`, then
   `A&`. (We do not make a copy.)

 It is important to pay attention to the value category in order to avoid a
 dangling reference if a function constructs a combination of awaitables and
 then returns it. Typically, the return value of `GetAwaitable(T&&)` should be
 used to initialize an object of type `AwaitableType<T&&>`; `AwaitableType` will
 be a value type or lvalue reference, but not a rvalue reference.
*/
template <class T>
auto GetAwaitable(T&& t) -> decltype(auto);

//! Returns the type that `GetAwaitable()` would return, stripped of any
//! rvalue-reference part (so you might get `T` or `T&`, but not `T&&`). This is
//! the appropriate type to store in an object that wraps another awaitable(s).
template <class Aw>
using AwaitableType = RemoveRvalueReferenceT<decltype(GetAwaitable(std::declval<Aw>()))>;

//! A quality-of-life adapter allowing passing lambdas returning `Co<>` instead
//! of tasks themselves, saving on a bunch of parentheses, not driving
//! clang-indent crazy, and (most importantly) not exposing / users to problems
//! with lifetimes of lambda object themselves.
template <class Callable>
class AwaitableLambda {
    using TaskT = std::invoke_result_t<Callable>;
    using AwaitableT = AwaitableType<TaskT>;

public:
    explicit AwaitableLambda(Callable&& c) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        : callable_(std::forward<Callable>(c))
    {
    }

    // NB: these forwarders are specialized for `TaskAwaitable`, and would need
    // generalization to support non-Task awaitables

    // We know that a `TaskAwaitable` will be not-ready (except `NoOp()`, but
    // that one doesn't mind if you suspend on it anyway). We need to initialize
    // the awaitable before `await_resume()` gets called, can't do it here since
    // the method is const, and `await_set_executor()` only runs if we're going
    // to suspend.
    [[nodiscard]] auto await_ready() const noexcept { return false; }

    void await_set_executor(Executor* ex) noexcept
    {
        GetAwaitable().await_set_executor(ex);
    }

    auto await_suspend(Handle h) { return awaitable_.await_suspend(h); }

    auto await_resume() -> decltype(auto)
    {
        return std::forward<AwaitableT>(awaitable_).await_resume();
    }

    auto await_early_cancel() noexcept
    {
        return GetAwaitable().await_early_cancel();
    }

    auto await_cancel(Handle h) noexcept { return awaitable_.await_cancel(h); }

    [[nodiscard]] auto await_must_resume() const noexcept
    {
        return awaitable_.await_must_resume();
    }

private:
    auto GetAwaitable() -> AwaitableT&
    {
        if (!task_.IsValid()) {
            task_ = callable_();
            awaitable_ = task_.operator co_await();
        }

        return awaitable_;
    }

    Callable callable_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    TaskT task_;
    AwaitableT awaitable_;
};

template <class Callable>
    requires(std::derived_from<std::invoke_result_t<Callable>, CoTag>)
auto operator co_await(Callable&& c)
{
    return AwaitableLambda<Callable>(std::forward<Callable>(c));
}

// The AwaitableLambda `operator co_await()` is not found via ADL and was not
// declared before `concept Awaitable`, so we need to specialize
// `ThisIsAwaitableTrustMe` in order to make callables returning Task<T> satisfy
// `Awaitable`. Note that compiler-generated `co_await` logic outside `namespace
// co::detail` would similarly not find it, but since our
// `BasePromise::await_transform()` uses `co::detail::GetAwaitable()`, tasks can
// await lambdas. We specifically _don't_ want to enable this for foreign tasks,
// because they won't know to call `await_set_executor`, which prevents
// AwaitableLambda from working.
template <class Callable, class Ret>
    requires(std::derived_from<std::invoke_result_t<Callable>, CoTag>
                && (std::same_as<Ret, Unspecified>
                    || std::convertible_to<
                        typename std::invoke_result_t<Callable>::ReturnType,
                        Ret>))
constexpr bool kThisIsAwaitableTrustMe<Callable, Ret> = true;

/// A utility awaitable to perform a function with the current task
/// temporarily suspended.
/// Can be used to add a suspension point.
template <class Callable, class ReturnType>
class [[nodiscard]] YieldToRunImpl : private Callable {
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
class [[nodiscard]] YieldToRunImpl<Callable, ReturnType&&> : private Callable {
public:
    explicit YieldToRunImpl(Callable cb)
        : Callable(std::move(cb))
    {
    }

    [[nodiscard]] auto await_ready() const noexcept { return false; }
    [[nodiscard]] auto await_suspend([[maybe_unused]] Handle h)
    {
        result_ = &(Callable::operator()());
        return false;
    }
    auto await_resume() -> ReturnType&& { return static_cast<ReturnType&&>(result_); }
    void await_introspect(auto& c) const noexcept { c.node("Yield"); }

private:
    ReturnType* result_ = nullptr;
};

template <class Callable>
class [[nodiscard]] YieldToRunImpl<Callable, void> : private Callable {
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

// NB: Must return by value.
template <ValidDirectAwaitable Aw>
auto StaticAwaitableCheck(Aw&& aw) -> Aw // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
{
    return std::forward<Aw>(aw);
}

template <class T>
auto GetAwaitable(T&& t) -> decltype(auto)
{
    static_assert(oxygen::co::Awaitable<T>, "tried to co_await on not an awaitable");

    if constexpr (
        requires() {
            { std::forward<T>(t) } -> DirectAwaitable;
        }) {
        // Explicit template argument so we can preserve a provided rvalue
        // reference rather than moving into a new object. The referent was
        // created in the co_await expression so it will live long enough for us
        // and we can save a copy. Without the explicit argument, universal
        // reference rules would infer Aw = T (which is what we want for the
        // other calls where we're passing a temporary that was created in this
        // function).
        return StaticAwaitableCheck<T&&>(std::forward<T>(t));
    } else if constexpr (
        requires() {
            { std::forward<T>(t).operator co_await() } -> DirectAwaitable;
        }) {
        return StaticAwaitableCheck(std::forward<T>(t).operator co_await());
    } else if constexpr (
        requires() {
            { operator co_await(std::forward<T>(t)) } -> DirectAwaitable;
        }) {
        return StaticAwaitableCheck(operator co_await(std::forward<T>(t)));
    } else {
        // if !Awaitable<T>, then the static_assert above fired, and we don't
        // need to fire this one also
        static_assert(!Awaitable<T>,
            "co_await argument satisfies Awaitable concept but "
            "we couldn't extract a `DirectAwaitable` from it");
        return std::suspend_never {};
    }
}

} // namespace oxygen::co::detail
