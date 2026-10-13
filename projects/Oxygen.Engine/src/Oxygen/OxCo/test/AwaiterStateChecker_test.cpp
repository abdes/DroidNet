// Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
// SPDX-License-Identifier: BSD-3-Clause

#include <coroutine>
#include <type_traits>
#include <utility>

#include "Utils/TestEventLoop.h"

#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/AwaitFn.h>
#include <Oxygen/OxCo/Detail/AwaiterStateChecker.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::co::detail::AwaiterStateChecker;
using WrappedNonCancellable
  = oxygen::co::detail::SanitizedAwaiter<std::suspend_always>;
static_assert(oxygen::co::detail::Cancellable<WrappedNonCancellable>);
static_assert(oxygen::co::detail::CustomizesMustResume<WrappedNonCancellable>);
using CancellableSleep
  = oxygen::co::testing::TestEventLoop::SleepAwaitable<true>;
using NonCancellableSleep
  = oxygen::co::testing::TestEventLoop::SleepAwaitable<false>;
static_assert(oxygen::co::detail::ValidAwaiter<CancellableSleep>);
static_assert(oxygen::co::detail::ValidAwaiter<NonCancellableSleep>);
static_assert(!oxygen::co::detail::CustomizesMustResume<CancellableSleep>);
static_assert(oxygen::co::detail::CustomizesMustResume<NonCancellableSleep>);
static_assert(std::is_same_v<decltype(oxygen::co::detail::AwaitMustResume(
                               std::declval<const CancellableSleep&>())),
  std::false_type>);

struct DiscardAfterEarlyCancel {
  int* must_resume_calls { nullptr };

  auto await_early_cancel() noexcept -> bool { return false; }
  auto await_ready() const noexcept -> bool { return true; }
  void await_suspend(oxygen::co::detail::Handle) noexcept { }
  auto await_cancel(oxygen::co::detail::Handle) noexcept -> bool
  {
    return false;
  }
  auto await_must_resume() const noexcept -> std::false_type
  {
    ++*must_resume_calls;
    return {};
  }
  void await_resume() noexcept { }
};

NOLINT_TEST(AwaiterStateChecker, DiscardableMustResumeHookRunsOnce)
{
  int calls = 0;
  oxygen::co::detail::SanitizedAwaiter awaiter(
    DiscardAfterEarlyCancel { .must_resume_calls = &calls });
  EXPECT_FALSE(awaiter.await_early_cancel());
  EXPECT_TRUE(awaiter.await_ready());
  EXPECT_FALSE(awaiter.await_must_resume());
  EXPECT_EQ(calls, 1);
}

#if defined(OXCO_AWAITER_STATE_DEBUG)

NOLINT_TEST(AwaiterStateChecker, ImmediateCompletionAndEarlyCancellation)
{
  AwaiterStateChecker checker;
  EXPECT_TRUE(checker.ReadyReturned(true));
  checker.AboutToResume();
  checker.Reset();
  EXPECT_TRUE(checker.EarlyCancelReturned(true));
  checker.Reset();
}

NOLINT_TEST(AwaiterStateChecker, SuspensionResumesThroughProxy)
{
  AwaiterStateChecker checker;
  EXPECT_FALSE(checker.ReadyReturned(false));
  checker.AboutToSetExecutor();
  const auto parent = std::noop_coroutine();
  const auto proxy = checker.AboutToSuspend(parent);
  EXPECT_NE(proxy.address(), parent.address());
  proxy.resume();
  checker.AboutToResume();
}

NOLINT_TEST(AwaiterStateChecker, CancellationCanCompleteOrResume)
{
  for (const bool must_resume : { false, true }) {
    AwaiterStateChecker checker;
    EXPECT_FALSE(checker.EarlyCancelReturned(false));
    EXPECT_FALSE(checker.ReadyReturned(false));
    checker.AboutToSetExecutor();
    const auto proxy = checker.AboutToSuspend(std::noop_coroutine());
    proxy.resume();
    EXPECT_EQ(checker.MustResumeReturned(must_resume), must_resume);
    if (must_resume) {
      checker.AboutToResume();
    }
  }
}

NOLINT_TEST(AwaiterStateChecker, CancellationDuringSuspension)
{
  AwaiterStateChecker checker;
  EXPECT_FALSE(checker.ReadyReturned(false));
  checker.AboutToSetExecutor();
  const auto parent = std::noop_coroutine();
  const auto proxy = checker.AboutToSuspend(parent);
  EXPECT_EQ(checker.AboutToCancel(parent), proxy);
  EXPECT_TRUE(checker.CancelReturned(true));
}

NOLINT_TEST(AwaiterStateChecker, ThrowingSuspendAndExplicitAbandonment)
{
  AwaiterStateChecker checker;
  EXPECT_FALSE(checker.ReadyReturned(false));
  checker.AboutToSetExecutor();
  static_cast<void>(checker.AboutToSuspend(std::noop_coroutine()));
  checker.SuspendThrew();
  checker.Reset();
  EXPECT_FALSE(checker.ReadyReturned(false));
  checker.AboutToSetExecutor();
  static_cast<void>(checker.AboutToSuspend(std::noop_coroutine()));
  checker.ForceReset();
}

NOLINT_TEST(AwaiterStateCheckerDeathTest, RejectsInvalidResume)
{
  EXPECT_DEATH(
    {
      AwaiterStateChecker checker;
      checker.AboutToResume();
    },
    "state");
}

NOLINT_TEST(AwaiterStateCheckerDeathTest, RejectsInvalidTransition)
{
  EXPECT_DEATH(
    {
      AwaiterStateChecker checker;
      EXPECT_TRUE(checker.EarlyCancelReturned(true));
      static_cast<void>(checker.ReadyReturned(false));
    },
    "Invalid awaiter state transition");
}

NOLINT_TEST(AwaiterStateCheckerDeathTest, RejectsSuspensionWithoutExecutor)
{
  EXPECT_DEATH(
    {
      AwaiterStateChecker checker;
      static_cast<void>(checker.ReadyReturned(false));
      static_cast<void>(checker.AboutToSuspend(std::noop_coroutine()));
    },
    "has_executor");
}

#else

static_assert(std::is_empty_v<AwaiterStateChecker>);
NOLINT_TEST(AwaiterStateChecker, DisabledCheckerPassesHandlesThrough)
{
  AwaiterStateChecker checker;
  const auto parent = std::noop_coroutine();
  EXPECT_EQ(checker.AboutToSuspend(parent), parent);
  EXPECT_EQ(checker.AboutToCancel(parent), parent);
}

#endif
} // namespace
