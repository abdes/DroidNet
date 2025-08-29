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
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Headless/CommandRecorder.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Surface.h>

// Use the module's exported API symbol to create the backend instance in-test.
// Declare the exported function here; the test links against the headless
// module project, which provides the symbol at link time.
extern "C" void* GetGraphicsModuleApi();

namespace {

using ::testing::Test;

//! Basic smoke fixture that creates/destroys the headless backend.
class HeadlessSmokeTest : public Test {
protected:
  void SetUp() override { }
  void TearDown() override { }
};

//! Test that creating the backend, allocating simple resources, and submitting
//! a trivial command list advances the queue fence.
NOLINT_TEST_F(
  HeadlessSmokeTest, CreateBackend_CreateResources_SubmitAdvancesFence)
{
  // Arrange
  auto module_ptr = static_cast<oxygen::graphics::GraphicsModuleApi*>(
    ::GetGraphicsModuleApi());
  ASSERT_NE(module_ptr, nullptr);

  oxygen::SerializedBackendConfig cfg { "{}", 2 };
  void* backend = module_ptr->CreateBackend(cfg);
  ASSERT_NE(backend, nullptr);

  // Cast to the headless concrete type so we can use its public helpers.
  auto* headless
    = reinterpret_cast<oxygen::graphics::headless::Graphics*>(backend);
  ASSERT_NE(headless, nullptr);

  // Create a command queue using the headless implementation.
  auto queue = headless->CreateCommandQueue("test-queue",
    oxygen::graphics::QueueRole::kGraphics,
    oxygen::graphics::QueueAllocationPreference::kAllInOne);
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
    ::oxygen::PixelExtent new_size { .width = 16u, .height = 8u };
    headless_surface->SetSize(new_size);

    // Attach a (possibly null) renderer so the surface allocates its
    // per-slot backbuffers. Passing a null shared_ptr is acceptable for
    // headless tests.
    headless_surface->AttachRenderer(
      std::shared_ptr<oxygen::graphics::RenderController> {});

    // Force a resize and validate that backbuffers are recreated.
    headless_surface->Resize();

    // Verify present semantics: record current slot, present a few times and
    // check the current back buffer index advances modulo kFramesInFlight.
    const uint32_t frames = ::oxygen::frame::kFramesInFlight.get();
    const uint32_t before = headless_surface->GetCurrentBackBufferIndex();
    for (uint32_t i = 1; i <= frames + 1; ++i) {
      headless_surface->Present();
      const uint32_t after = headless_surface->GetCurrentBackBufferIndex();
      // The index should advance by 1 each present (modulo frames)
      const uint32_t expected = (before + i) % frames;
      EXPECT_EQ(after, expected);
    }

    // Check that GetBackBuffer(slot) returns a non-null texture for each
    // valid slot.
    for (uint32_t s = 0; s < frames; ++s) {
      auto bb = headless_surface->GetBackBuffer(s);
      EXPECT_NE(bb, nullptr);
    }
  }

  // Create a lightweight CommandList (common implementation) and a headless
  // recorder.
  auto cmd_list = std::make_unique<oxygen::graphics::CommandList>(
    "test-cmdlist", oxygen::graphics::QueueRole::kGraphics);
  ASSERT_NE(cmd_list, nullptr);
  auto recorder = std::make_unique<oxygen::graphics::headless::CommandRecorder>(
    cmd_list.get(), queue.get());
  ASSERT_NE(recorder, nullptr);

  // Create a simple buffer and texture via headless factories.
  oxygen::graphics::BufferDesc buf_desc {};
  buf_desc.size_bytes = 1024;
  buf_desc.debug_name = "smoke-buffer";
  auto buffer = headless->CreateBuffer(buf_desc);
  ASSERT_NE(buffer, nullptr);

  oxygen::graphics::TextureDesc tex_desc {};
  tex_desc.width = 4;
  tex_desc.height = 4;
  tex_desc.format = oxygen::Format::kUnknown;
  tex_desc.debug_name = "smoke-texture";
  auto texture = headless->CreateTexture(tex_desc);
  ASSERT_NE(texture, nullptr);

  // Track initial states for both resources: register them with the device
  // registry and begin tracking. Factories return shared_ptrs, which is the
  // form expected by ResourceRegistry::Register.
  recorder->Begin();
  // After Begin, the command list should be in Recording state.
  EXPECT_TRUE(cmd_list->IsRecording());
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

  // End recording, flush barriers and submit
  recorder->End();
  // After End, the command list should be Closed.
  EXPECT_TRUE(cmd_list->IsClosed());
  queue->Submit(*cmd_list);

  // Wait for submission completion using the queue fence. Headless Submit
  // advances the fence synchronously, but explicitly wait to mirror real
  // backend usage patterns.
  const auto signaled = queue->GetCompletedValue();
  queue->Wait(signaled);

  // Assert: fence advanced (>= 1)
  EXPECT_GE(signaled, 1u);

  // Unregister resources from the device registry and release them.
  headless->GetResourceRegistry().UnRegisterResource(*buffer);
  headless->GetResourceRegistry().UnRegisterResource(*texture);

  // Release shared_ptrs to the resources
  buffer.reset();
  texture.reset();

  // Cleanup
  module_ptr->DestroyBackend();
}

} // namespace
