//===----------------------------------------------------------------------===//
// Smoke test for the headless graphics backend.
// Verifies that the loader can create the backend, create simple resources,
// submit a trivial command list and advance the queue fence.
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Headless/CommandRecorder.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Surface.h>

// Use the module's exported API symbol to create the backend instance in-test.
// Declare the exported function here; the test links against the headless
// module project, which provides the symbol at link time.
extern "C" auto GetGraphicsModuleApi() -> void*;

namespace {

using testing::Test;

using Role = oxygen::graphics::QueueRole;
using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueSpecification;

//! Basic smoke fixture that creates/destroys the headless backend.
class HeadlessSmokeTest : public Test {
protected:
  auto SetUp() -> void override { }
  auto TearDown() -> void override { }
};

//! Test that creating the backend, allocating simple resources, and submitting
//! a trivial command list advances the queue fence.
NOLINT_TEST_F(HeadlessSmokeTest, TypicalUsage)
{
  // Arrange
  auto module_ptr
    = static_cast<oxygen::graphics::GraphicsModuleApi*>(GetGraphicsModuleApi());
  ASSERT_NE(module_ptr, nullptr);

  oxygen::SerializedBackendConfig cfg { .json_data = "{}", .size = 2 };
  oxygen::SerializedPathFinderConfig path_cfg { .json_data = "{}", .size = 2 };
  void* backend = module_ptr->CreateBackend(cfg, path_cfg);
  ASSERT_NE(backend, nullptr);

  // Cast to the headless concrete type so we can use its public helpers.
  auto* headless = static_cast<oxygen::graphics::headless::Graphics*>(backend);
  ASSERT_NE(headless, nullptr);

  // Create command queues using a multi-named strategy so multiple named
  // queues exist and AcquireCommandRecorder can find the requested named
  // queue. Use the MultiNamedStrategy defined in headless queue tests.
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
  auto queue = headless->GetCommandQueue(q_key);
  ASSERT_NE(queue, nullptr);

  // Also exercise HeadlessSurface behaviors: create a surface, set size,
  // trigger a resize and validate present/slot semantics.
  auto surface = headless->CreateSurface(
    /*window_weak=*/std::weak_ptr<oxygen::platform::Window> {},
    /*command_queue=*/queue);
  ASSERT_NE(surface, nullptr);

  // Try to use headless-only helpers by casting to the concrete type.
  if (auto* headless_surface
    = static_cast<oxygen::graphics::headless::HeadlessSurface*>(
      surface.get())) {
    // Use a strong PixelExtent type for SetSize.
    oxygen::PixelExtent new_size { .width = 16u, .height = 8u };
    headless_surface->SetSize(new_size);

    // Force a resize and validate that backbuffers are recreated.
    headless_surface->Resize();

    // Verify present semantics: record current slot, present a few times and
    // check the current back buffer index advances modulo kFramesInFlight.
    constexpr auto frames = oxygen::frame::kFramesInFlight.get();
    const auto before = headless_surface->GetCurrentBackBufferIndex();
    for (auto i = 1u; i <= frames + 1; ++i) {
      headless_surface->Present();
      const auto after = headless_surface->GetCurrentBackBufferIndex();
      // The index should advance by 1 each present (modulo frames)
      const auto expected = (before + i) % frames;
      EXPECT_EQ(after, expected);
    }

    // Check that GetBackBuffer(slot) returns a non-null texture for each
    // valid slot.
    for (auto s = 0u; s < frames; ++s) {
      auto bb = headless_surface->GetBackBuffer(s);
      EXPECT_NE(bb, nullptr);
    }
  }

  // Create a simple buffer and texture via headless factories. Keep the
  // shared pointers in this scope so we can unregister them after the recorder
  // has been submitted by its custom deleter.
  oxygen::graphics::BufferDesc buf_desc {
    .size_bytes = 1024,
    .debug_name = "smoke-buffer",
  };
  auto buffer = headless->CreateBuffer(buf_desc);
  ASSERT_NE(buffer, nullptr);

  oxygen::graphics::TextureDesc tex_desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kUnknown,
    .debug_name = "smoke-texture",
    .clear_value = oxygen::graphics::Color {},
  };
  auto texture = headless->CreateTexture(tex_desc);
  ASSERT_NE(texture, nullptr);

  // Acquire the recorder and perform recording inside a scope. When the
  // recorder goes out of scope the headless custom deleter will End(),
  // Submit() and call OnSubmitted() (immediate_submission=true).
  // Use virtual GetCommandQueue so backends can override lookup/fallback
  // behavior (for example, falling back to a QueueManager). This keeps the
  // higher-level contract clean and allows backend-specific policies.

  auto cmd_list_name = "test-cmd-list";
  auto cmd_list
    = headless->AcquireCommandList(queue->GetQueueRole(), cmd_list_name);
  ASSERT_NE(cmd_list, nullptr);

  const auto before_value = queue->GetCurrentValue();
  const auto completion_value = before_value + 1;
  {
    auto recorder = headless->AcquireCommandRecorder(
      q_key, cmd_list_name, /*immediate_submission=*/true);
    ASSERT_NE(recorder, nullptr);

    // Track initial states for both resources: register them with the
    // device registry and begin tracking. Factories return shared pointers,
    // which is the form expected by ResourceRegistry::Register.
    // AcquireCommandRecorder already calls Begin() on the returned recorder.
    headless->GetResourceRegistry().Register(buffer);
    recorder->BeginTrackingResourceState(
      *buffer, oxygen::graphics::ResourceStates::kUnknown);

    headless->GetResourceRegistry().Register(texture);
    recorder->BeginTrackingResourceState(
      *texture, oxygen::graphics::ResourceStates::kUnknown);

    // Require the buffer to become a copy destination (should produce a
    // transition)
    recorder->RequireResourceState(
      *buffer, oxygen::graphics::ResourceStates::kCopyDest);

    // Require the texture to have UnorderedAccess (UAV) but it already is in
    // the same state — this should trigger a memory barrier insertion path
    recorder->RequireResourceState(
      *texture, oxygen::graphics::ResourceStates::kUnorderedAccess);

    DLOG_F(2, "Smoke: expected completion value: {}", completion_value);
    recorder->RecordQueueSignal(completion_value);

    // End of scope triggers the custom deleter which will submit the
    // recorded commands immediately.
  }

  // Wait for completion.
  try {
    queue->Wait(completion_value);
    LOG_F(INFO, "Smoke: submission execution completed");
    cmd_list->OnExecuted();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "Smoke: wait for completion value failed: {}", e.what());
  }

  cmd_list.reset();

  // The headless deleter reserved a tail value (current+1) and submitted the
  // recorder; therefore the queue should have advanced by at least one from
  // the value we observed before creating the recorder.
  EXPECT_GE(queue->GetCompletedValue(), before_value + 1);
  queue.reset();

  // Unregister resources from the device registry and release them.
  headless->GetResourceRegistry().UnRegisterResource(*buffer);
  headless->GetResourceRegistry().UnRegisterResource(*texture);

  // Release shared pointers to the resources
  buffer.reset();
  texture.reset();

  // Cleanup
  module_ptr->DestroyBackend();
}

} // namespace
