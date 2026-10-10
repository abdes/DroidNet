//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::graphics::QueueRole;
using oxygen::graphics::d3d12::CommandQueue;
using oxygen::graphics::d3d12::testing::OffscreenTestFixture;

class SubmitOrderedQueueActionTest : public OffscreenTestFixture {
protected:
  [[nodiscard]] auto GetD3D12Queue(
    const QueueRole role = QueueRole::kGraphics) const -> CommandQueue&
  {
    auto* queue = static_cast<CommandQueue*>(GetQueue(role).get());
    CHECK_NOTNULL_F(queue);
    return *queue;
  }
};

NOLINT_TEST_F(SubmitOrderedQueueActionTest,
  RecordQueueSignalDoesNotAdvanceFenceBeforeDeferredSubmit)
{
  auto& queue = GetD3D12Queue();
  EXPECT_EQ(queue.GetCurrentValue(), 0U);
  EXPECT_EQ(queue.GetCompletedValue(), 0U);

  {
    auto recorder = AcquireRecorder("deferred-queue-signal",
      QueueRole::kGraphics, oxygen::graphics::SubmissionPolicy::kExplicit);
    CHECK_F(static_cast<bool>(recorder));
    recorder->RecordQueueSignal(1);
    KeepPendingRecording(std::move(recorder));
  }

  EXPECT_EQ(queue.GetCurrentValue(), 0U);
  EXPECT_EQ(queue.GetCompletedValue(), 0U);

  SubmitPendingRecordings();
  queue.Wait(1);

  EXPECT_EQ(queue.GetCurrentValue(), 1U);
  EXPECT_EQ(queue.GetCompletedValue(), 1U);
}

NOLINT_TEST_F(SubmitOrderedQueueActionTest,
  RecordQueueSignalAdvancesFenceAfterImmediateSubmit)
{
  auto& queue = GetD3D12Queue();

  {
    auto recorder = AcquireRecorder("immediate-queue-signal");
    CHECK_F(static_cast<bool>(recorder));
    recorder->RecordQueueSignal(1);
  }

  queue.Wait(1);

  EXPECT_EQ(queue.GetCurrentValue(), 1U);
  EXPECT_EQ(queue.GetCompletedValue(), 1U);
}

NOLINT_TEST_F(
  SubmitOrderedQueueActionTest, DeferredSubmitPreservesSignalOrderAcrossLists)
{
  auto& queue = GetD3D12Queue();

  {
    auto recorder = AcquireRecorder("ordered-signal-first",
      QueueRole::kGraphics, oxygen::graphics::SubmissionPolicy::kExplicit);
    CHECK_F(static_cast<bool>(recorder));
    recorder->RecordQueueSignal(1);
    KeepPendingRecording(std::move(recorder));
  }
  {
    auto recorder = AcquireRecorder("ordered-signal-second",
      QueueRole::kGraphics, oxygen::graphics::SubmissionPolicy::kExplicit);
    CHECK_F(static_cast<bool>(recorder));
    recorder->RecordQueueSignal(2);
    KeepPendingRecording(std::move(recorder));
  }

  EXPECT_EQ(queue.GetCurrentValue(), 0U);
  EXPECT_EQ(queue.GetCompletedValue(), 0U);

  SubmitPendingRecordings();
  queue.Wait(2);

  EXPECT_EQ(queue.GetCurrentValue(), 2U);
  EXPECT_EQ(queue.GetCompletedValue(), 2U);
}

//! Assigned signals follow the submission order, so a recording may submit
//! after one recorded later.
NOLINT_TEST_F(
  SubmitOrderedQueueActionTest, AssignedSignalsFollowTheSubmissionOrder)
{
  auto& queue = GetD3D12Queue();
  auto first = AcquireRecorder("assigned-first", QueueRole::kGraphics,
    oxygen::graphics::SubmissionPolicy::kExplicit);
  auto second = AcquireRecorder("assigned-second", QueueRole::kGraphics,
    oxygen::graphics::SubmissionPolicy::kExplicit);
  CHECK_F(static_cast<bool>(first) && static_cast<bool>(second));
  const auto first_signal = first->RecordAssignedQueueSignal();
  const auto second_signal = second->RecordAssignedQueueSignal();
  EXPECT_EQ(first_signal->Value(), 0U);

  ASSERT_TRUE(second.Submit());
  ASSERT_TRUE(first.Submit());
  queue.Wait(first_signal->Value());

  EXPECT_GT(second_signal->Value(), 0U);
  EXPECT_GT(first_signal->Value(), second_signal->Value());
  EXPECT_EQ(queue.GetCompletedValue(), first_signal->Value());
}

NOLINT_TEST_F(
  SubmitOrderedQueueActionTest, RecordQueueWaitHonorsSatisfiedFenceAtSubmitTime)
{
  auto& queue = GetD3D12Queue();
  {
    auto recorder = AcquireRecorder("initial-signal");
    CHECK_F(static_cast<bool>(recorder));
    recorder->RecordQueueSignal(1);
  }
  queue.Wait(1);

  {
    auto recorder = AcquireRecorder("wait-then-signal", QueueRole::kGraphics,
      oxygen::graphics::SubmissionPolicy::kExplicit);
    CHECK_F(static_cast<bool>(recorder));
    recorder->RecordQueueWait(1);
    recorder->RecordQueueSignal(2);
    KeepPendingRecording(std::move(recorder));
  }

  SubmitPendingRecordings();
  queue.Wait(2);

  EXPECT_EQ(queue.GetCurrentValue(), 2U);
  EXPECT_EQ(queue.GetCompletedValue(), 2U);
}

} // namespace
