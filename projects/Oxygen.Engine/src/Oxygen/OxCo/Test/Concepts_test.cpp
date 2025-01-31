//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Concepts/Awaitable.h"
#include "Oxygen/OxCo/Concepts/Awaiter.h"
#include "Oxygen/OxCo/Concepts/Promise.h"

// ReSharper disable CppClangTidyClangDiagnosticUnneededMemberFunction

namespace {
struct BasicAwaiter {
    static auto await_ready() -> bool { return false; }
    static void await_suspend(std::coroutine_handle<>) { }
    static void await_resume() { }
};
struct ChainAwaiter {
    static auto await_ready() -> bool { return false; }
    static auto await_suspend(const std::coroutine_handle<> h) { return h; }
    static void await_resume() { }
};
struct ResumeAwaiter {
    static auto await_ready() -> bool { return false; }
    static auto await_suspend(std::coroutine_handle<>) -> bool { return false; }
    static void await_resume() { }
};
struct ValueAwaiter {
    static auto await_ready() -> bool { return false; }
    static auto await_suspend(std::coroutine_handle<>) -> bool { return true; }
    static auto await_resume() -> int { return 44; }
};

static_assert(oxygen::co::Awaiter<BasicAwaiter>);
static_assert(oxygen::co::Awaiter<ChainAwaiter>);
static_assert(oxygen::co::Awaiter<ResumeAwaiter>);
static_assert(oxygen::co::DirectAwaitable<ValueAwaiter, int>);

struct ValueAwaitable {
    auto operator co_await() const -> ValueAwaiter
    {
        return {};
    }
};

struct ResumeAwaitable {
    auto operator co_await() const -> ResumeAwaiter
    {
        return {};
    }
};
static_assert(oxygen::co::MemberCoAwaitAwaitable<ValueAwaitable>);
static_assert(!oxygen::co::MemberCoAwaitAwaitableVoid<ValueAwaitable>);
static_assert(oxygen::co::MemberCoAwaitAwaitableVoid<ResumeAwaitable>);

static_assert(
    std::is_same_v<oxygen::co::AwaitableTraits<ValueAwaitable>::AwaiterType,
        ValueAwaiter>);
static_assert(
    std::is_same_v<oxygen::co::AwaitableTraits<ValueAwaitable>::AwaiterReturnType,
        int>);

struct BasicPromise {
    static auto get_return_object() -> std::coroutine_handle<>
    {
        return {};
    }

    static auto initial_suspend() -> std::suspend_always
    {
        return {};
    }

    static auto final_suspend() -> std::suspend_always
    {
        return {};
    }

    static void return_void() { }
    static void unhandled_exception() { }
};

static_assert(oxygen::co::DirectAwaitable<std::suspend_always>);
static_assert(oxygen::co::PromiseType<BasicPromise, void>);

struct ValuePromise {
    static auto get_return_object() -> std::coroutine_handle<>
    {
        return {};
    }

    static auto initial_suspend() -> std::suspend_always
    {
        return {};
    }

    static auto final_suspend() -> std::suspend_always
    {
        return {};
    }

    static void return_value(const int v) { (void)v; }
    static void unhandled_exception() { }
};

static_assert(oxygen::co::PromiseType<ValuePromise, int>);

} // namespace
