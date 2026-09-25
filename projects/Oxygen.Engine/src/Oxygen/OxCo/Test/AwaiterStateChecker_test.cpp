// Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
// SPDX-License-Identifier: BSD-3-Clause

#include <coroutine>
#include <type_traits>

#include <Oxygen/OxCo/Detail/AwaiterStateChecker.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::co::detail::AwaiterStateChecker;
using WrappedNonCancellable
  = oxygen::co::detail::SanitizedAwaiter<std::suspend_always>;
static_assert(oxygen::co::detail::Cancellable<WrappedNonCancellable>);
static_assert(oxygen::co::detail::CustomizesMustResume<WrappedNonCancellable>);

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
