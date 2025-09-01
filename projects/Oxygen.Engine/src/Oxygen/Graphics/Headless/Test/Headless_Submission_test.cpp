//===----------------------------------------------------------------------===//
// Tests for deferred submission behavior in the headless graphics backend.
// Verifies that when a recorder is created with immediate_submission=false the
// recorded command list is not submitted until SubmitDeferredCommandLists()
// is called on the headless Graphics instance.
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Headless/CommandRecorder.h>
#include <Oxygen/Graphics/Headless/Graphics.h>

extern "C" auto GetGraphicsModuleApi() -> void*;

namespace {

using testing::Test;

using Role = oxygen::graphics::QueueRole;
using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueSpecification;

// Create default queues using a small local strategy similar to the smoke
// test so we can find by name.
class LocalMultiNamedStrategy : public oxygen::graphics::QueuesStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return {
      QueueSpecification {
        .key = QueueKey { "multi-gfx" },
        .role = Role::kGraphics,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kNamed,
      },
      {
        .key = QueueKey { "multi-cpu" },
        .role = Role::kCompute,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kNamed,
      },
    };
  }

  [[nodiscard]] auto KeyFor(const Role role) const -> QueueKey override
  {
    switch (role) {
    case Role::kGraphics:
    case Role::kTransfer:
    case Role::kPresent:
      return QueueKey { "multi-gfx" };
    case Role::kCompute:
      return QueueKey { "multi-cpu" };
    case oxygen::graphics::QueueRole::kMax:;
    }
    return QueueKey { "__invalid__" };
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<LocalMultiNamedStrategy>(*this);
  }
};

class HeadlessSubmissionTest : public Test {
protected:
  auto SetUp() -> void override { }
  auto TearDown() -> void override { }
};

NOLINT_TEST_F(HeadlessSubmissionTest, DeferredBasic)
{
  auto module_ptr
    = static_cast<oxygen::graphics::GraphicsModuleApi*>(GetGraphicsModuleApi());
  ASSERT_NE(module_ptr, nullptr);

  oxygen::SerializedBackendConfig cfg { "{}", 2 };
  void* backend = module_ptr->CreateBackend(cfg);
  ASSERT_NE(backend, nullptr);

  auto* headless = static_cast<oxygen::graphics::headless::Graphics*>(backend);
  ASSERT_NE(headless, nullptr);
  LocalMultiNamedStrategy queue_strategy;
  headless->CreateCommandQueues(queue_strategy);
  auto queue
    = headless->GetCommandQueue(queue_strategy.KeyFor(Role::kGraphics));
  ASSERT_NE(queue, nullptr);

  // Acquire a command list and record a single signal into it. Use
  // immediate_submission=false so the deleter will not submit automatically.
  auto cmd_list
    = headless->AcquireCommandList(queue->GetQueueRole(), "deferred-cmd-list");
  ASSERT_NE(cmd_list, nullptr);

  const auto before_value = queue->GetCurrentValue();
  const auto completion_value = before_value + 1;
  {
    auto recorder
      = headless->AcquireCommandRecorder(oxygen::observer_ptr { queue.get() },
        cmd_list, /*immediate_submission=*/false);
    ASSERT_NE(recorder, nullptr);

    // Begin tracking nothing heavy; just record a queue signal and exit
    // scope so the deleter stores the completed command list for later.
    recorder->RecordQueueSignal(completion_value);
  }

  // At this point the queue should NOT have advanced because we deferred
  // the submission.
  EXPECT_LT(queue->GetCompletedValue(), completion_value);

  // Trigger deferred submission and wait for completion.
  headless->SubmitDeferredCommandLists();
  try {
    queue->Wait(completion_value);
    cmd_list->OnExecuted();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "DeferredBasic: wait failed: {}", e.what());
  }

  EXPECT_GE(queue->GetCompletedValue(), completion_value);

  // Cleanup
  cmd_list.reset();
  queue.reset();
  module_ptr->DestroyBackend();
}

} // namespace
