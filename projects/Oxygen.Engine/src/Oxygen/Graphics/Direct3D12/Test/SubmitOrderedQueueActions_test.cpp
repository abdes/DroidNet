//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>

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
