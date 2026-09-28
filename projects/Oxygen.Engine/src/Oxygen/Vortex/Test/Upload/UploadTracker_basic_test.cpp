//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>

#if defined(_MSC_VER) && defined(_DEBUG)
#  include <new>

#  include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#endif

#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>
#include <Oxygen/Vortex/Upload/UploadTracker.h>

namespace {
using namespace std::chrono_literals;
using oxygen::vortex::upload::FenceValue;
using oxygen::vortex::upload::UploadError;
using oxygen::vortex::upload::UploadResult;
using oxygen::vortex::upload::UploadTicket;
using oxygen::vortex::upload::UploadTracker;

NOLINT_TEST(UploadTrackerTest, RegisterAndComplete)
{
  UploadTracker tracker;
  const auto first = tracker.Register(FenceValue { 5 }, 128);
  const auto second = tracker.Register(FenceValue { 7 }, 256);
  EXPECT_FALSE(first.TryGetResult());
  EXPECT_FALSE(second.TryGetResult());
  tracker.MarkFenceCompleted(first.Fence());
  EXPECT_TRUE(first.Await().success);
  EXPECT_EQ(first.Await().bytes_uploaded, 128U);
  EXPECT_FALSE(second.TryGetResult());
  tracker.MarkFenceCompleted(second.Fence());
  EXPECT_EQ(second.Await().bytes_uploaded, 256U);
  EXPECT_FALSE(second.Cancel());
}

NOLINT_TEST(UploadTrackerTest, CopiesRetainResultsAcrossProgressAndDestruction)
{
  auto tracker = std::make_unique<UploadTracker>();
  auto original = tracker->Register(FenceValue { 5 }, 128);
  const auto copy = original;
  auto moved = std::move(original);
  EXPECT_EQ(original.Await().error, UploadError::kTicketNotFound);
  EXPECT_FALSE(original.Cancel());
  EXPECT_EQ(copy.Id(), moved.Id());
  for (auto fence = 0U; fence < 1000U; ++fence) {
    tracker->MarkFenceCompleted(FenceValue { fence });
  }
  tracker.reset();
  EXPECT_TRUE(copy.Await().success);
  EXPECT_EQ(moved.Await().bytes_uploaded, 128U);
}

NOLINT_TEST(UploadTrackerTest, EqualIdsFromDifferentTrackersStayIndependent)
{
  UploadTracker first_tracker;
  UploadTracker second_tracker;
  const auto first = first_tracker.Register(FenceValue { 5 }, 32);
  const auto second = second_tracker.Register(FenceValue { 5 }, 64);
  ASSERT_EQ(first.Id(), second.Id());
  first_tracker.MarkFenceCompleted(first.Fence());
  EXPECT_TRUE(first.Await().success);
  EXPECT_FALSE(second.TryGetResult());
  EXPECT_TRUE(second.Cancel());
  EXPECT_TRUE(first.Await().success);
}

NOLINT_TEST(UploadTrackerTest, BlockingWaitWakesOnCompletion)
{
  UploadTracker tracker;
  const auto ticket = tracker.Register(FenceValue { 10 }, 42);
  auto future
    = std::async(std::launch::async, [ticket]() { return ticket.Await(); });
  EXPECT_EQ(future.wait_for(0ms), std::future_status::timeout);
  tracker.MarkFenceCompleted(ticket.Fence());
  EXPECT_EQ(future.get().bytes_uploaded, 42U);
}

NOLINT_TEST(UploadTrackerTest, BlockingWaitWakesOnOwnerDestruction)
{
  auto tracker = std::make_unique<UploadTracker>();
  const auto ticket = tracker->Register(FenceValue { 10 }, 42);
  auto future
    = std::async(std::launch::async, [ticket]() { return ticket.Await(); });
  tracker.reset();
  EXPECT_EQ(future.get().error, UploadError::kTrackerShutdown);
  EXPECT_FALSE(ticket.Cancel());
}

NOLINT_TEST(UploadTrackerTest, CancellationWakesLogicalWaitAndSurvivesClose)
{
  UploadTracker tracker;
  const auto ticket = tracker.Register(FenceValue { 10 }, 42);
  const auto copy = ticket;
  auto future
    = std::async(std::launch::async, [ticket]() { return ticket.Await(); });
  EXPECT_TRUE(copy.Cancel());
  EXPECT_FALSE(ticket.Cancel());
  EXPECT_EQ(future.get().error, UploadError::kCanceled);
  tracker.MarkFenceCompleted(ticket.Fence());
  tracker.Close();
  EXPECT_EQ(ticket.Await().error, UploadError::kCanceled);
}

NOLINT_TEST(UploadTrackerTest, CompletedAndFailedResultsSurviveClose)
{
  UploadTracker tracker;
  const auto complete = tracker.Register(FenceValue { 5 }, 128);
  const auto pending = tracker.Register(FenceValue { 10 }, 256);
  const auto failed
    = tracker.RegisterFailedImmediate(UploadError::kProducerFailed);
  tracker.MarkFenceCompleted(complete.Fence());
  tracker.Close();
  tracker.MarkFenceCompleted(pending.Fence());
  EXPECT_TRUE(complete.Await().success);
  EXPECT_EQ(failed.Await().error, UploadError::kProducerFailed);
  EXPECT_EQ(pending.Await().error, UploadError::kTrackerShutdown);
  EXPECT_EQ(tracker.CompletedFence(), complete.Fence());
}

NOLINT_TEST(UploadTrackerTest, DeviceLossDoesNotInventSuccessfulCompletion)
{
  UploadTracker tracker;
  const auto complete = tracker.Register(FenceValue { 5 }, 128);
  const auto pending = tracker.Register(FenceValue { 10 }, 256);
  tracker.MarkFenceCompleted(complete.Fence());
  tracker.MarkFenceCompleted(
    FenceValue { std::numeric_limits<std::uint64_t>::max() });
  EXPECT_TRUE(complete.Await().success);
  EXPECT_EQ(pending.Await().error, UploadError::kDeviceLost);
  EXPECT_EQ(tracker.CompletedFence(), complete.Fence());
}

NOLINT_TEST(
  UploadTrackerTest, SubmissionHighwaterSurvivesDroppedAndOutOfOrderTickets)
{
  UploadTracker tracker;
  (void)tracker.Register(FenceValue { 100 }, 32);
  (void)tracker.Register(FenceValue { 50 }, 16);
  tracker.MarkFenceCompleted(FenceValue { 77 });
  (void)tracker.RegisterFailedImmediate(UploadError::kProducerFailed);
  EXPECT_EQ(tracker.LastSubmittedFence().get(), 100U);
  tracker.MarkFenceCompleted(FenceValue { 50 });
  EXPECT_EQ(tracker.CompletedFence().get(), 77U);
}

NOLINT_TEST(
  UploadTrackerTest, TemporaryTicketIsRetainedBeforeCoroutineExecution)
{
  UploadTracker tracker;
  auto wait = tracker.Register(FenceValue { 5 }, 64).AwaitGpuCompletionAsync();
  tracker.MarkFenceCompleted(FenceValue { 5 });
  oxygen::co::testing::TestEventLoop loop;
  const auto result = oxygen::co::Run(loop, std::move(wait));
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.bytes_uploaded, 64U);
}

NOLINT_TEST(UploadTrackerTest, AsyncCancellationStillWaitsForPhysicalCompletion)
{
  UploadTracker tracker;
  const auto ticket = tracker.Register(FenceValue { 5 }, 64);
  oxygen::co::testing::TestEventLoop loop;
  bool physically_completed = false;
  loop.Schedule(1ms, [ticket]() { EXPECT_TRUE(ticket.Cancel()); });
  loop.Schedule(2ms, [&]() {
    physically_completed = true;
    tracker.MarkFenceCompleted(ticket.Fence());
  });
  const auto result = oxygen::co::Run(loop, ticket.AwaitGpuCompletionAsync());
  EXPECT_TRUE(physically_completed);
  EXPECT_EQ(result.error, UploadError::kCanceled);
}

auto CloseAfterCompletion(UploadTicket ticket,
  std::unique_ptr<UploadTracker>& tracker) -> oxygen::co::Co<UploadResult>
{
  const auto result = co_await ticket.AwaitGpuCompletionAsync();
  tracker.reset();
  co_return result;
}

auto AdvanceAfterCompletion(UploadTicket ticket, UploadTracker& tracker)
  -> oxygen::co::Co<UploadResult>
{
  const auto result = co_await ticket.AwaitGpuCompletionAsync();
  tracker.MarkFenceCompleted(FenceValue { 20 });
  co_return result;
}

NOLINT_TEST(UploadTrackerTest, ReentrantDestructionWakesLaterFenceWaiter)
{
  auto tracker = std::make_unique<UploadTracker>();
  const auto first = tracker->Register(FenceValue { 5 }, 32);
  const auto later = tracker->Register(FenceValue { 20 }, 64);
  oxygen::co::testing::TestEventLoop loop;
  loop.Schedule(1ms, [&]() { tracker->MarkFenceCompleted(first.Fence()); });
  const auto results = oxygen::co::Run(loop,
    oxygen::co::AllOf(
      CloseAfterCompletion(first, tracker), later.AwaitGpuCompletionAsync()));
  EXPECT_FALSE(tracker);
  EXPECT_TRUE(std::get<0>(results).success);
  EXPECT_EQ(std::get<1>(results).error, UploadError::kTrackerShutdown);
}

NOLINT_TEST(UploadTrackerTest, ReentrantProgressWakesLaterFenceWaiter)
{
  UploadTracker tracker;
  const auto first = tracker.Register(FenceValue { 5 }, 32);
  const auto later = tracker.Register(FenceValue { 20 }, 64);
  oxygen::co::testing::TestEventLoop loop;
  loop.Schedule(1ms, [&]() { tracker.MarkFenceCompleted(first.Fence()); });
  const auto results = oxygen::co::Run(loop,
    oxygen::co::AllOf(
      AdvanceAfterCompletion(first, tracker), later.AwaitGpuCompletionAsync()));
  EXPECT_TRUE(std::get<0>(results).success);
  EXPECT_TRUE(std::get<1>(results).success);
}

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST(
  UploadTrackerTest, FailedResultAllocationPreservesSubmissionHighwater)
{
  UploadTracker tracker;
  bool failed = false;
  {
    const oxygen::graphics::testing::HeapAllocationFailure deny_allocations;
    try {
      (void)tracker.Register(FenceValue { 100 }, 32);
    } catch (const std::bad_alloc&) {
      failed = true;
    }
  }
  EXPECT_TRUE(failed);
  EXPECT_EQ(tracker.LastSubmittedFence().get(), 100U);
}
#endif

} // namespace
