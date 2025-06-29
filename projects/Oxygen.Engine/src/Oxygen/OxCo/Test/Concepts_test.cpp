//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Coroutine.h>

// ReSharper disable CppClangTidyClangDiagnosticUnneededMemberFunction

namespace {
struct BasicAwaiter {
  static auto await_ready() -> bool { return false; }
  static void await_suspend(std::coroutine_handle<> /*h*/) { }
  static void await_resume() { }
};
struct ChainAwaiter {
  static auto await_ready() -> bool { return false; }
  static auto await_suspend(const std::coroutine_handle<> h) { return h; }
  static void await_resume() { }
};
struct ResumeAwaiter {
  static auto await_ready() -> bool { return false; }
  static auto await_suspend(std::coroutine_handle<> /*h*/) -> bool
  {
    return false;
  }
  static void await_resume() { }
};
struct ValueAwaiter {
  static auto await_ready() -> bool { return false; }
  static auto await_suspend(std::coroutine_handle<> /*h*/) -> bool
  {
    return true;
  }
  static auto await_resume() -> int { return 44; }
};

static_assert(oxygen::co::Awaiter<BasicAwaiter>);
static_assert(oxygen::co::Awaiter<ChainAwaiter>);
static_assert(oxygen::co::Awaiter<ResumeAwaiter>);
static_assert(oxygen::co::ImmediateAwaitable<ValueAwaiter, int>);

struct BasicPromise {
  static auto get_return_object() -> std::coroutine_handle<> { return {}; }

  static auto initial_suspend() -> std::suspend_always { return {}; }

  static auto final_suspend() noexcept -> std::suspend_always { return {}; }

  static void return_void() { }
  static void unhandled_exception() { }
};

static_assert(oxygen::co::ImmediateAwaitable<std::suspend_always>);
static_assert(oxygen::co::PromiseType<BasicPromise, void>);

struct ValuePromise {
  static auto get_return_object() -> std::coroutine_handle<> { return {}; }

  static auto initial_suspend() -> std::suspend_always { return {}; }

  static auto final_suspend() noexcept -> std::suspend_always { return {}; }

  static void return_value(const int v) { (void)v; }
  static void unhandled_exception() { }
};

static_assert(oxygen::co::PromiseType<ValuePromise, int>);

} // namespace
