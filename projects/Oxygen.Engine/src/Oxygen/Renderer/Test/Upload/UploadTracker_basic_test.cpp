//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadTracker.h>

#include <array>
#include <chrono>
#include <thread>

namespace {

using oxygen::engine::upload::FenceValue;
using oxygen::engine::upload::TicketId;
using oxygen::engine::upload::UploadTicket;
using oxygen::engine::upload::UploadTracker;

//! Verify registration and marking fence completion propagates to tickets.
NOLINT_TEST(UploadTracker, RegisterAndComplete)
{
  // Arrange
  UploadTracker tracker;
  constexpr FenceValue f1 { 5 };
  constexpr FenceValue f2 { 7 };

  // Act
  const auto t1 = tracker.Register(f1, /*bytes*/ 128, "t1");
  const auto t2 = tracker.Register(f2, /*bytes*/ 256, "t2");

  // Assert pre-completion
  EXPECT_FALSE(tracker.IsComplete(t1.id));
  EXPECT_FALSE(tracker.IsComplete(t2.id));
  EXPECT_FALSE(tracker.TryGetResult(t1.id).has_value());

  // Act: complete up to f1
  tracker.MarkFenceCompleted(FenceValue { 5 });

  // Assert: t1 completed, t2 pending
  EXPECT_TRUE(tracker.IsComplete(t1.id));
  EXPECT_FALSE(tracker.IsComplete(t2.id));
  const auto r1 = tracker.TryGetResult(t1.id);
  if (!r1.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_TRUE(r1->success);
  EXPECT_EQ(r1->bytes_uploaded, 128u);

  // Act: complete up to f2
  tracker.MarkFenceCompleted(FenceValue { 7 });

  // Assert: t2 completed
  EXPECT_TRUE(tracker.IsComplete(t2.id));
  const auto r2 = tracker.TryGetResult(t2.id);
  if (!r2.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_TRUE(r2->success);
  EXPECT_EQ(r2->bytes_uploaded, 256u);
}

//! Await(id) blocks until completion and returns the populated result.
NOLINT_TEST(UploadTracker, AwaitSingle)
{
  // Arrange
  UploadTracker tracker;
  const auto t = tracker.Register(FenceValue { 10 }, 42, "single");

  // Act (pre): in another thread, mark completion after a brief delay
  std::thread worker([&tracker]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tracker.MarkFenceCompleted(FenceValue { 10 });
  });

  // Assert: Await returns populated result
  const auto r = tracker.Await(t.id);
  EXPECT_TRUE(r.success);
  EXPECT_EQ(r.bytes_uploaded, 42u);

  worker.join();
}

//! AwaitAll waits for the max fence across tickets and returns in-order
//! results.
NOLINT_TEST(UploadTracker, AwaitAllMaxFence)
{
  // Arrange
  UploadTracker tracker;
  const auto t1 = tracker.Register(FenceValue { 2 }, 10, "a");
  const auto t2 = tracker.Register(FenceValue { 5 }, 20, "b");
  const std::array<UploadTicket, 2> tickets { t1, t2 };

  // Act: complete first, ensure not all done yet
  tracker.MarkFenceCompleted(FenceValue { 2 });
  EXPECT_TRUE(tracker.IsComplete(t1.id));
  EXPECT_FALSE(tracker.IsComplete(t2.id));

  // In another thread, complete later
  std::thread worker([&tracker]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tracker.MarkFenceCompleted(FenceValue { 5 });
  });

  const auto results = tracker.AwaitAll(std::span<const UploadTicket>(tickets));
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].bytes_uploaded, 10u);
  EXPECT_EQ(results[1].bytes_uploaded, 20u);
  EXPECT_TRUE(results[0].success);
  EXPECT_TRUE(results[1].success);

  worker.join();
}

//! CompletedFenceValue is monotonic and never regresses on lower values.
NOLINT_TEST(UploadTracker, CompletedFenceMonotonic)
{
  // Arrange
  UploadTracker tracker;
  EXPECT_EQ(tracker.CompletedFence().get(), 0u);

  // Act + Assert
  tracker.MarkFenceCompleted(FenceValue { 1 });
  EXPECT_EQ(tracker.CompletedFence().get(), 1u);

  tracker.MarkFenceCompleted(FenceValue { 3 });
  EXPECT_EQ(tracker.CompletedFence().get(), 3u);

  // Lower values should not regress
  tracker.MarkFenceCompleted(FenceValue { 2 });
  EXPECT_EQ(tracker.CompletedFence().get(), 3u);
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional tests for Cancel and GetStats
//===----------------------------------------------------------------------===//

NOLINT_TEST(UploadTracker, Cancel_Pending_MarksCanceled)
{
  using oxygen::engine::upload::UploadError;
  UploadTracker tracker;
  // Register a ticket with a future fence
  const auto t = tracker.Register(FenceValue { 100 }, 123, "to-cancel");
  // Cancel before completion
  const bool canceled = tracker.Cancel(t.id);
  EXPECT_TRUE(canceled);
  // Should be complete now with Canceled error
  EXPECT_TRUE(tracker.IsComplete(t.id));
  auto r = tracker.TryGetResult(t.id);
  if (!r.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_FALSE(r->success);
  EXPECT_EQ(r->error, UploadError::kCanceled);
  EXPECT_EQ(r->bytes_uploaded, 0u);
}

NOLINT_TEST(UploadTracker, GetStats_Counters_Advance)
{
  UploadTracker tracker;
  [[maybe_unused]] const auto t1 = tracker.Register(FenceValue { 1 }, 10, "a");
  [[maybe_unused]] const auto t2 = tracker.Register(FenceValue { 2 }, 20, "b");
  auto stats = tracker.GetStats();
  EXPECT_EQ(stats.submitted, 2u);
  EXPECT_EQ(stats.in_flight, 2u);
  EXPECT_EQ(stats.bytes_submitted, 30u);

  tracker.MarkFenceCompleted(FenceValue { 1 });
  stats = tracker.GetStats();
  EXPECT_EQ(stats.completed, 1u);
  EXPECT_EQ(stats.in_flight, 1u);
  EXPECT_EQ(stats.bytes_completed, 10u);

  tracker.MarkFenceCompleted(FenceValue { 2 });
  stats = tracker.GetStats();
  EXPECT_EQ(stats.completed, 2u);
  EXPECT_EQ(stats.in_flight, 0u);
  EXPECT_EQ(stats.bytes_completed, 30u);
}
