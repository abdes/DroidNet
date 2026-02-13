//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Headless/Graphics.h>

// Declare module entry symbol used by smoke test.
extern "C" auto GetGraphicsModuleApi() -> void*;

namespace {

using Role = oxygen::graphics::QueueRole;
using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueSpecification;

NOLINT_TEST(ResourceBarrierExecution, AppliesObservedState)
{
  auto module_ptr
    = static_cast<oxygen::graphics::GraphicsModuleApi*>(GetGraphicsModuleApi());
  ASSERT_NE(module_ptr, nullptr);

  oxygen::SerializedBackendConfig cfg { .json_data = "{}", .size = 2 };
  oxygen::SerializedPathFinderConfig path_cfg { .json_data = "{}", .size = 2 };
  void* backend = module_ptr->CreateBackend(cfg, path_cfg);
  ASSERT_NE(backend, nullptr);

  auto* headless = static_cast<oxygen::graphics::headless::Graphics*>(backend);
  ASSERT_NE(headless, nullptr);

  // Local multi-named strategy used in smoke test so AcquireCommandRecorder
  // can locate named queues.
  class LocalMultiNamedStrategy : public oxygen::graphics::QueuesStrategy {
  public:
    [[nodiscard]] auto Specifications() const
      -> std::vector<QueueSpecification> override
    {
      return {
        {
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
  } queue_strategy;

  headless->CreateCommandQueues(queue_strategy);

  auto q_key = queue_strategy.KeyFor(Role::kGraphics);
  auto cmd_list_name = "test-cmd-list";
  auto queue = headless->GetCommandQueue(q_key);
  ASSERT_NE(queue, nullptr);

  auto cmd_list
    = headless->AcquireCommandList(queue->GetQueueRole(), cmd_list_name);
  ASSERT_NE(cmd_list, nullptr);

  // Create a buffer and keep it alive for the duration of the test (like the
  // smoke test does), so we can unregister it after submission.
  oxygen::graphics::BufferDesc buf_desc {
    .size_bytes = 128,
    .debug_name = "test-buffer",
  };
  auto buffer = headless->CreateBuffer(buf_desc);
  ASSERT_NE(buffer, nullptr);

  const auto before_value = queue->GetCurrentValue();
  const auto completion_value = before_value + 1;
  {
    auto recorder = headless->AcquireCommandRecorder(
      q_key, cmd_list_name, /*immediate_submission=*/true);
    ASSERT_NE(recorder, nullptr);

    // Register and begin tracking the buffer with the recorder.
    headless->GetResourceRegistry().Register(buffer);
    recorder->BeginTrackingResourceState(
      *buffer, oxygen::graphics::ResourceStates::kUnknown);

    recorder->RequireResourceState(
      *buffer, oxygen::graphics::ResourceStates::kCopyDest);

    recorder->FlushBarriers();

    recorder->RecordQueueSignal(completion_value);

    // End of scope: recorder custom deleter will End(), Submit() and
    // OnSubmitted().
  }

  // Wait for submission completion using the queue's wait API like the smoke
  // test does. Then mark the command list as executed.
  try {
    queue->Wait(completion_value);
    cmd_list->OnExecuted();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "Wait failed: {}", e.what());
  }

  EXPECT_GE(queue->GetCompletedValue(), before_value + 1);

  // Release the command list before destroying the backend (matches smoke test)
  cmd_list.reset();

  // Unregister and cleanup the buffer and backend.
  headless->GetResourceRegistry().UnRegisterResource(*buffer);
  buffer.reset();
  module_ptr->DestroyBackend();
}

} // namespace
