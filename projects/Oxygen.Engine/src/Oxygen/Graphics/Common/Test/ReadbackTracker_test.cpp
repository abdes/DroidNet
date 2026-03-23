//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <future>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/ReadbackTracker.h>

using oxygen::SizeBytes;
using oxygen::graphics::FenceValue;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackTracker;

namespace {

struct ReadbackTrackerTest : testing::Test {
  ReadbackTracker tracker;
};

NOLINT_TEST_F(ReadbackTrackerTest, Register_CreatesPendingTicket)
{
  const auto ticket
    = tracker.Register(FenceValue { 42 }, SizeBytes { 128 }, "buffer readback");
  const auto is_complete = tracker.IsComplete(ticket.id);
  const auto result = tracker.TryGetResult(ticket.id);

  EXPECT_EQ(ticket.fence.get(), 42U);
  ASSERT_TRUE(is_complete.has_value());
  EXPECT_FALSE(*is_complete);
  EXPECT_FALSE(result.has_value());
  EXPECT_TRUE(tracker.HasPending());
}

NOLINT_TEST_F(ReadbackTrackerTest, RegisterFailedImmediate_MarksFailed)
{
  const auto ticket = tracker.RegisterFailedImmediate(
    "buffer readback", ReadbackError::kInvalidArgument);
  const auto is_complete = tracker.IsComplete(ticket.id);

  ASSERT_TRUE(is_complete.has_value());
  EXPECT_TRUE(*is_complete);

  const auto result = tracker.TryGetResult(ticket.id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ticket.id.get(), ticket.id.get());
  ASSERT_TRUE(result->error.has_value());
  EXPECT_EQ(*result->error, ReadbackError::kInvalidArgument);
  EXPECT_FALSE(tracker.HasPending());
}

NOLINT_TEST_F(ReadbackTrackerTest, MarkFenceCompleted_CompletesEligibleTickets)
{
  const auto ticket_a
    = tracker.Register(FenceValue { 10 }, SizeBytes { 16 }, "first");
  const auto ticket_b
    = tracker.Register(FenceValue { 11 }, SizeBytes { 32 }, "second");

  tracker.MarkFenceCompleted(FenceValue { 10 });

  const auto complete_a = tracker.IsComplete(ticket_a.id);
  const auto complete_b = tracker.IsComplete(ticket_b.id);
  ASSERT_TRUE(complete_a.has_value());
  ASSERT_TRUE(complete_b.has_value());
  EXPECT_TRUE(*complete_a);
  EXPECT_FALSE(*complete_b);

  const auto result = tracker.TryGetResult(ticket_a.id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ticket.id.get(), ticket_a.id.get());
  EXPECT_EQ(result->bytes_copied.get(), 16U);
}

NOLINT_TEST_F(ReadbackTrackerTest, Cancel_MarksTicketCancelled)
{
  const auto ticket
    = tracker.Register(FenceValue { 7 }, SizeBytes { 64 }, "cancel me");

  const auto cancelled = tracker.Cancel(ticket.id);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(cancelled.value());

  const auto second_cancel = tracker.Cancel(ticket.id);
  ASSERT_TRUE(second_cancel.has_value());
  EXPECT_FALSE(second_cancel.value());

  const auto result = tracker.TryGetResult(ticket.id);
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->error.has_value());
  EXPECT_EQ(*result->error, ReadbackError::kCancelled);
  EXPECT_FALSE(tracker.HasPending());
}

NOLINT_TEST_F(ReadbackTrackerTest, OnFrameStart_RemovesCurrentFrameEntries)
{
  tracker.OnFrameStart(oxygen::frame::Slot { 1 });
  const auto ticket
    = tracker.Register(FenceValue { 1 }, SizeBytes { 8 }, "frame scoped");

  EXPECT_TRUE(tracker.IsComplete(ticket.id).has_value());
  tracker.OnFrameStart(oxygen::frame::Slot { 1 });

  const auto is_complete = tracker.IsComplete(ticket.id);
  const auto result = tracker.TryGetResult(ticket.id);
  EXPECT_FALSE(is_complete.has_value());
  EXPECT_EQ(is_complete.error(), ReadbackError::kTicketNotFound);
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(ReadbackTrackerTest, AwaitAllPending_WaitsForCompletion)
{
  const auto ticket
    = tracker.Register(FenceValue { 33 }, SizeBytes { 24 }, "wait me");

  auto pending_future = std::async(
    std::launch::async, [&]() { return tracker.AwaitAllPending(); });

  EXPECT_EQ(pending_future.wait_for(std::chrono::milliseconds { 1 }),
    std::future_status::timeout);

  tracker.MarkFenceCompleted(FenceValue { 33 });

  const auto pending = pending_future.get();
  ASSERT_TRUE(pending.has_value());
  ASSERT_EQ(pending->size(), 1U);
  EXPECT_EQ(pending->front().ticket.id.get(), ticket.id.get());
  EXPECT_EQ(pending->front().bytes_copied.get(), 24U);

  const auto result = tracker.Await(ticket.id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ticket.id.get(), ticket.id.get());
  EXPECT_EQ(result->bytes_copied.get(), 24U);
}

} // namespace
