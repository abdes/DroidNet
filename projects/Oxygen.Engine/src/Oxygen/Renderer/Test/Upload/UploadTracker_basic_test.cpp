//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadTracker.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#include <array>
#include <chrono>
#include <thread>

// Implementation of UploaderTagFactory. Provides access to UploaderTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal
#endif

namespace {

using oxygen::engine::upload::FenceValue;
using oxygen::engine::upload::TicketId;
using oxygen::engine::upload::UploadTicket;
using oxygen::engine::upload::UploadTracker;
using oxygen::engine::upload::internal::UploaderTagFactory;
using oxygen::frame::Slot;

//! Verify registration and marking fence completion propagates to tickets.
NOLINT_TEST(UploadTrackerTest, RegisterAndComplete)
{
  // Arrange
  UploadTracker tracker;
  constexpr FenceValue f1 { 5 };
  constexpr FenceValue f2 { 7 };

  // Act
  const auto t1 = tracker.Register(f1, /*bytes*/ 128, "t1");
  const auto t2 = tracker.Register(f2, /*bytes*/ 256, "t2");

  // Assert pre-completion
  {
    const auto is_t1 = tracker.IsComplete(t1.id);
    ASSERT_TRUE(is_t1.has_value());
    EXPECT_FALSE(is_t1.value());
  }
  {
    const auto is_t2 = tracker.IsComplete(t2.id);
    ASSERT_TRUE(is_t2.has_value());
    EXPECT_FALSE(is_t2.value());
  }
  EXPECT_FALSE(tracker.TryGetResult(t1.id).has_value());

  // Act: complete up to f1
  tracker.MarkFenceCompleted(FenceValue { 5 });

  // Assert: t1 completed, t2 pending
  {
    const auto is_t1 = tracker.IsComplete(t1.id);
    ASSERT_TRUE(is_t1.has_value());
    EXPECT_TRUE(is_t1.value());
  }
  {
    const auto is_t2 = tracker.IsComplete(t2.id);
    ASSERT_TRUE(is_t2.has_value());
    EXPECT_FALSE(is_t2.value());
  }
  const auto r1 = tracker.TryGetResult(t1.id);
  if (!r1.has_value()) {
    FAIL() << "Expected result for t1 after completion";
  }
  EXPECT_TRUE(r1->success);
  EXPECT_EQ(r1->bytes_uploaded, 128u);

  // Act: complete up to f2
  tracker.MarkFenceCompleted(FenceValue { 7 });

  // Assert: t2 completed
  {
    const auto is_t2 = tracker.IsComplete(t2.id);
    ASSERT_TRUE(is_t2.has_value());
    EXPECT_TRUE(is_t2.value());
  }
  const auto r2 = tracker.TryGetResult(t2.id);
  if (!r2.has_value()) {
    FAIL() << "Expected result for t2 after completion";
  }
  EXPECT_TRUE(r2->success);
  EXPECT_EQ(r2->bytes_uploaded, 256u);
}

//! Await(id) blocks until completion and returns the populated result.
NOLINT_TEST(UploadTrackerTest, AwaitSingle)
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
  const auto await_result = tracker.Await(t.id);
  ASSERT_TRUE(await_result.has_value());
  const auto r = await_result.value();
  EXPECT_TRUE(r.success);
  EXPECT_EQ(r.bytes_uploaded, 42u);

  worker.join();
}

//! AwaitAll waits for the max fence across tickets and returns in-order
//! results.
NOLINT_TEST(UploadTrackerTest, AwaitAllMaxFence)
{
  // Arrange
  UploadTracker tracker;
  const auto t1 = tracker.Register(FenceValue { 2 }, 10, "a");
  const auto t2 = tracker.Register(FenceValue { 5 }, 20, "b");
  const std::array<UploadTicket, 2> tickets { t1, t2 };

  // Act: complete first, ensure not all done yet
  tracker.MarkFenceCompleted(FenceValue { 2 });
  {
    const auto is_t1 = tracker.IsComplete(t1.id);
    ASSERT_TRUE(is_t1.has_value());
    EXPECT_TRUE(is_t1.value());
  }
  {
    const auto is_t2 = tracker.IsComplete(t2.id);
    ASSERT_TRUE(is_t2.has_value());
    EXPECT_FALSE(is_t2.value());
  }

  // In another thread, complete later
  std::thread worker([&tracker]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tracker.MarkFenceCompleted(FenceValue { 5 });
  });

  const auto await_all_result
    = tracker.AwaitAll(std::span<const UploadTicket>(tickets));
  ASSERT_TRUE(await_all_result.has_value());
  const auto& results = await_all_result.value();
  ASSERT_EQ(results.size(), tickets.size());
  EXPECT_EQ(results[0].bytes_uploaded, 10u);
  EXPECT_EQ(results[1].bytes_uploaded, 20u);
  EXPECT_TRUE(results[0].success);
  EXPECT_TRUE(results[1].success);

  worker.join();
}

//! CompletedFenceValue is monotonic and never regresses on lower values.
NOLINT_TEST(UploadTrackerTest, CompletedFenceMonotonic)
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

//! LastRegisteredFence reflects the last fence value registered by
//! Track/Register
NOLINT_TEST(UploadTrackerTest, LastRegisteredFence_TracksRegister)
{
  // Arrange
  UploadTracker tracker;

  // Initially zero
  EXPECT_EQ(tracker.LastRegisteredFence().get(), 0u);

  // Register two tickets and assert LastRegisteredFence reflects the most
  // recently registered fence value.
  const auto t1 = tracker.Register(FenceValue { 5 }, 10, "r1");
  EXPECT_EQ(tracker.LastRegisteredFence().get(), 5u);

  const auto t2 = tracker.Register(FenceValue { 12 }, 20, "r2");
  EXPECT_EQ(tracker.LastRegisteredFence().get(), 12u);
}

//! RegisterFailedImmediate should update last-registered fence to the
//! tracker's completed fence value.
NOLINT_TEST(UploadTrackerTest, LastRegisteredFence_UpdatedOnFailedImmediate)
{
  UploadTracker tracker;

  // Simulate some completion in the past and verify RegisterFailedImmediate
  // stores the completed fence value.
  tracker.MarkFenceCompleted(FenceValue { 77 });

  const auto failed = tracker.RegisterFailedImmediate(
    "failing", oxygen::engine::upload::UploadError::kCanceled);

  EXPECT_EQ(tracker.LastRegisteredFence().get(), 77u);
  // And ensure the returned ticket's fence matches the recorded completed fence
  EXPECT_EQ(failed.fence.get(), 77u);
}

//! Verify OnFrameStart erases entries created in the same frame slot.
NOLINT_TEST(UploadTrackerTest, OnFrameStart_CleansEntries)
{
  // Arrange
  UploadTracker tracker;

  // Register two tickets in different slots by simulating frame starts.
  tracker.OnFrameStart(UploaderTagFactory::Get(), Slot { 1 });
  const auto t1 = tracker.Register(FenceValue { 10 }, 11, "slot1");

  tracker.OnFrameStart(UploaderTagFactory::Get(), Slot { 2 });
  const auto t2 = tracker.Register(FenceValue { 20 }, 22, "slot2");

  // Pre-condition: both tickets exist
  {
    const auto is_t1 = tracker.IsComplete(t1.id);
    ASSERT_TRUE(is_t1.has_value());
  }
  {
    const auto is_t2 = tracker.IsComplete(t2.id);
    ASSERT_TRUE(is_t2.has_value());
  }

  // Act: start frame for slot 1 again which should erase entries created in
  // slot 1 per OnFrameStart implementation.
  tracker.OnFrameStart(UploaderTagFactory::Get(), Slot { 1 });

  // Assert
  // t1 should be erased: IsComplete should return UploadError::kTicketNotFound
  const auto is_t1 = tracker.IsComplete(t1.id);
  ASSERT_FALSE(is_t1.has_value());
  EXPECT_EQ(
    is_t1.error(), oxygen::engine::upload::UploadError::kTicketNotFound);

  // t2 should still exist (not erased)
  const auto is_t2 = tracker.IsComplete(t2.id);
  ASSERT_TRUE(is_t2.has_value());
}

//! Best-effort cancellation should mark a pending ticket as canceled.
NOLINT_TEST(UploadTrackerTest, Cancel_Pending_MarksCanceled)
{
  // Arrange
  UploadTracker tracker;
  const auto t = tracker.Register(FenceValue { 100 }, 123, "to-cancel");

  // Act
  const auto cancel_result = tracker.Cancel(t.id);

  // Assert
  ASSERT_TRUE(cancel_result.has_value());
  const bool canceled = cancel_result.value();
  EXPECT_TRUE(canceled);
  {
    const auto is_complete = tracker.IsComplete(t.id);
    ASSERT_TRUE(is_complete.has_value());
    EXPECT_TRUE(is_complete.value());
  }
  const auto r = tracker.TryGetResult(t.id);
  if (!r.has_value()) {
    FAIL() << "Expected result after cancellation";
  }
  EXPECT_FALSE(r->success);
  EXPECT_EQ(r->error, oxygen::engine::upload::UploadError::kCanceled);
  EXPECT_EQ(r->bytes_uploaded, 0u);
}

} // namespace
