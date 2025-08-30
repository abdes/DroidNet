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

extern "C" void* GetGraphicsModuleApi();

namespace {

using ::testing::Test;

// Create default queues using a small local strategy similar to the smoke
// test so we can lookup by name.
class LocalMultiNamedStrategy : public oxygen::graphics::QueueStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    using oxygen::graphics::QueueAllocationPreference;
    using oxygen::graphics::QueueRole;
    using oxygen::graphics::QueueSharingPreference;
    using oxygen::graphics::QueueSpecification;
    return { {
               .name = "multi-gfx",
               .role = QueueRole::kGraphics,
               .allocation_preference = QueueAllocationPreference::kDedicated,
               .sharing_preference = QueueSharingPreference::kSeparate,
             },
      {
        .name = "multi-cpu",
        .role = QueueRole::kCompute,
        .allocation_preference = QueueAllocationPreference::kDedicated,
        .sharing_preference = QueueSharingPreference::kSeparate,
      } };
  }
  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return "multi-gfx";
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return "multi-gfx";
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return "multi-cpu";
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return "multi-gfx";
  }
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<LocalMultiNamedStrategy>(*this);
  }
};

class HeadlessSubmissionTest : public Test {
protected:
  void SetUp() override { }
  void TearDown() override { }
};

NOLINT_TEST_F(HeadlessSubmissionTest, DeferredBasic)
{
  auto module_ptr = static_cast<oxygen::graphics::GraphicsModuleApi*>(
    ::GetGraphicsModuleApi());
  ASSERT_NE(module_ptr, nullptr);

  oxygen::SerializedBackendConfig cfg { "{}", 2 };
  void* backend = module_ptr->CreateBackend(cfg);
  ASSERT_NE(backend, nullptr);

  auto* headless
    = reinterpret_cast<oxygen::graphics::headless::Graphics*>(backend);
  ASSERT_NE(headless, nullptr);
  LocalMultiNamedStrategy queue_strategy;
  headless->CreateCommandQueues(queue_strategy);
  auto queue = headless->GetCommandQueue(queue_strategy.GraphicsQueueName());
  ASSERT_NE(queue, nullptr);

  // Acquire a command list and record a single signal into it. Use
  // immediate_submission=false so the deleter will not submit automatically.
  auto cmd_list
    = headless->AcquireCommandList(queue->GetQueueRole(), "deferred-cmdlist");
  ASSERT_NE(cmd_list, nullptr);

  const auto before_value = queue->GetCurrentValue();
  const auto completion_value = before_value + 1;
  {
    auto recorder = headless->AcquireCommandRecorder(
      queue, cmd_list, /*immediate_submission=*/false);
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
