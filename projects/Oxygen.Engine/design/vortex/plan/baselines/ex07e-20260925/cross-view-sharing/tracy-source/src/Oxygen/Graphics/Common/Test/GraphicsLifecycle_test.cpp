//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <expected>
#include <limits>
#include <memory>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Mocks/MockGraphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::observer_ptr;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackManager;
using oxygen::graphics::ReadbackResult;
using oxygen::graphics::ReadbackTicket;
using TestGraphics
  = ::testing::NiceMock<::oxygen::graphics::testing::MockGraphics>;

class CountingReadbackManager final : public ReadbackManager {
public:
  CountingReadbackManager()
    : ReadbackManager()
  {
  }

  auto CreateBufferReadback(std::string_view /*debug_name*/)
    -> std::shared_ptr<oxygen::graphics::GpuBufferReadback> override
  {
    return {};
  }

  auto CreateTextureReadback(std::string_view /*debug_name*/)
    -> std::shared_ptr<oxygen::graphics::GpuTextureReadback> override
  {
    return {};
  }

  auto Await(ReadbackTicket /*ticket*/)
    -> std::expected<ReadbackResult, ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  auto AwaitAsync(ReadbackTicket /*ticket*/) -> oxygen::co::Co<void> override
  {
    co_return;
  }

  auto Cancel(ReadbackTicket /*ticket*/)
    -> std::expected<bool, ReadbackError> override
  {
    return false;
  }

  auto ReadBufferNow(
    const oxygen::graphics::Buffer& /*source*/, oxygen::graphics::BufferRange)
    -> std::expected<std::vector<std::byte>, ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  auto ReadTextureNow(const oxygen::graphics::Texture& /*source*/,
    oxygen::graphics::TextureReadbackRequest /*request*/, bool /*tightly_pack*/)
    -> std::expected<oxygen::graphics::OwnedTextureReadbackData,
      ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  auto CreateReadbackTextureSurface(
    const oxygen::graphics::TextureDesc& /*desc*/)
    -> std::expected<std::shared_ptr<oxygen::graphics::Texture>,
      ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  auto MapReadbackTextureSurface(oxygen::graphics::Texture& /*surface*/,
    oxygen::graphics::TextureSlice /*slice*/)
    -> std::expected<oxygen::graphics::ReadbackSurfaceMapping,
      ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  auto UnmapReadbackTextureSurface(oxygen::graphics::Texture& /*surface*/)
    -> void override
  {
  }

  auto OnFrameStart(const oxygen::frame::Slot slot) -> void override
  {
    ++on_frame_start_calls;
    last_slot = slot.get();
  }

  auto Shutdown(std::chrono::milliseconds /*timeout*/)
    -> std::expected<void, ReadbackError> override
  {
    ++shutdown_calls;
    return {};
  }

  int on_frame_start_calls { 0 };
  int shutdown_calls { 0 };
  uint32_t last_slot { (std::numeric_limits<uint32_t>::max)() };
};

NOLINT_TEST(GraphicsLifecycleTest, BeginFrame_ForwardsSlotToReadbackManager)
{
  TestGraphics gfx("Test Graphics");
  CountingReadbackManager readback_manager;

  ON_CALL(gfx, GetReadbackManager())
    .WillByDefault(
      ::testing::Return(observer_ptr<ReadbackManager> { &readback_manager }));

  gfx.BeginFrame(
    oxygen::frame::SequenceNumber { 7 }, oxygen::frame::Slot { 2 });

  EXPECT_EQ(readback_manager.on_frame_start_calls, 1);
  EXPECT_EQ(readback_manager.last_slot, 2U);
}

NOLINT_TEST(GraphicsLifecycleTest, BeginFrame_AllowsMissingReadbackManager)
{
  TestGraphics gfx("Test Graphics");

  ON_CALL(gfx, GetReadbackManager())
    .WillByDefault(::testing::Return(observer_ptr<ReadbackManager> {}));

  EXPECT_NO_THROW(gfx.BeginFrame(
    oxygen::frame::SequenceNumber { 1 }, oxygen::frame::Slot { 0 }));
}

NOLINT_TEST(GraphicsLifecycleTest, Stop_ForwardsShutdownToReadbackManager)
{
  TestGraphics gfx("Test Graphics");
  CountingReadbackManager readback_manager;

  ON_CALL(gfx, GetReadbackManager())
    .WillByDefault(
      ::testing::Return(observer_ptr<ReadbackManager> { &readback_manager }));

  gfx.Stop();

  EXPECT_EQ(readback_manager.shutdown_calls, 1);
}

NOLINT_TEST(GraphicsLifecycleTest, Stop_AllowsMissingReadbackManager)
{
  TestGraphics gfx("Test Graphics");

  ON_CALL(gfx, GetReadbackManager())
    .WillByDefault(::testing::Return(observer_ptr<ReadbackManager> {}));

  EXPECT_NO_THROW(gfx.Stop());
}

NOLINT_TEST(GraphicsLifecycleTest,
  CloseWithoutNurseryDrainsOnceAndRejectsRecordingAcquisition)
{
  TestGraphics gfx("Close without nursery");
  CountingReadbackManager readback_manager;
  ON_CALL(gfx, GetReadbackManager())
    .WillByDefault(
      ::testing::Return(observer_ptr<ReadbackManager> { &readback_manager }));
  int reclaimed = 0;
  gfx.GetDeferredReclaimer().RegisterDeferredAction([&] { ++reclaimed; });
  EXPECT_FALSE(gfx.IsRunning());
  gfx.Close();
  gfx.Close();
  EXPECT_EQ(readback_manager.shutdown_calls, 1);
  EXPECT_EQ(reclaimed, 1);
  EXPECT_EQ(gfx.GetBackendLifetime()->State(),
    oxygen::graphics::BackendLifecycle::kRetiring);
  EXPECT_THROW((void)gfx.AcquireCommandRecorder(
                 oxygen::graphics::QueueKey { "closed" }, "closed"),
    std::logic_error);
}

NOLINT_TEST(GraphicsLifecycleTest,
  Destructor_DrainsLateRetirementWhileCommonComponentsRemainAccessible)
{
  // MockGraphics has no early destructor drain. The Common Graphics destructor
  // must run these actions before Composition takes its exclusive teardown
  // lock.
  auto gfx = std::make_unique<TestGraphics>("Late retirement");
  auto* const owner = static_cast<oxygen::Graphics*>(gfx.get());
  auto* const registry = &owner->GetResourceRegistry();
  unsigned completed = 0U;
  gfx->Stop();
  gfx->Flush();
  for (unsigned slot = 0U; slot < oxygen::frame::kFramesInFlight.get();
    ++slot) {
    auto& reclaimer = gfx->GetDeferredReclaimer();
    reclaimer.OnBeginFrame(oxygen::frame::Slot { slot });
    reclaimer.RegisterDeferredAction([owner, registry, &completed]() {
      EXPECT_EQ(&owner->GetResourceRegistry(), registry);
      EXPECT_EQ(owner->GetResourceRegistry().GetRegisteredResourceCount(), 0U);
      EXPECT_EQ(owner->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics),
        nullptr);
      ++completed;
    });
  }
  EXPECT_EQ(completed, 0U);
  gfx.reset();
  EXPECT_EQ(completed, oxygen::frame::kFramesInFlight.get());
}

} // namespace
