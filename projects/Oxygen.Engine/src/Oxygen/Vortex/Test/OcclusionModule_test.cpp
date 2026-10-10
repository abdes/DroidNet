//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::ViewId;
using oxygen::graphics::QueueRole;
using oxygen::vortex::OcclusionConfig;
using oxygen::vortex::OcclusionModule;
using oxygen::vortex::PreparedSceneFrame;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::occlusion::internal::HistorySlotAllocator;
using oxygen::vortex::occlusion::internal::kFreshHistorySlotBit;
using oxygen::vortex::occlusion::internal::kNoHistorySlot;
using oxygen::vortex::testing::FakeGraphics;

using oxygen::graphics::GpuBufferReadback;
using oxygen::graphics::MappedBufferReadback;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackTicket;

//! A buffer readback the test completes with chosen result words.
class FakeBufferReadback final : public GpuBufferReadback {
public:
  FakeBufferReadback() = default;
  ~FakeBufferReadback() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeBufferReadback)
  OXYGEN_MAKE_NON_MOVABLE(FakeBufferReadback)

  auto EnqueueCopy(oxygen::graphics::CommandRecorder& /*recorder*/,
    const oxygen::graphics::Buffer& /*source*/,
    oxygen::graphics::BufferRange /*range*/)
    -> std::expected<ReadbackTicket, ReadbackError> override
  {
    state_ = ReadbackState::kPending;
    return ReadbackTicket {};
  }

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return state_;
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override
  {
    return state_ == ReadbackState::kReady;
  }

  auto TryMap() -> std::expected<MappedBufferReadback, ReadbackError> override
  {
    if (state_ != ReadbackState::kReady) {
      return std::unexpected(ReadbackError::kNotReady);
    }
    return MappedBufferReadback { {}, std::as_bytes(std::span { words_ }) };
  }

  auto MapNow() -> std::expected<MappedBufferReadback, ReadbackError> override
  {
    return TryMap();
  }

  auto Cancel() -> std::expected<bool, ReadbackError> override { return false; }

  auto Reset() -> void override
  {
    state_ = ReadbackState::kIdle;
    words_.clear();
  }

  auto ResetForReuse() -> std::expected<void, ReadbackError> override
  {
    Reset();
    return {};
  }

  //! Completes the pending copy with one visibility word per candidate.
  auto Complete(std::vector<std::uint32_t> words) -> void
  {
    words_ = std::move(words);
    state_ = ReadbackState::kReady;
  }

private:
  ReadbackState state_ { ReadbackState::kIdle };
  std::vector<std::uint32_t> words_;
};

//! Creates buffer readbacks and keeps them for the test to complete.
class FakeReadbackManager final : public oxygen::graphics::ReadbackManager {
public:
  FakeReadbackManager() = default;
  ~FakeReadbackManager() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeReadbackManager)
  OXYGEN_MAKE_NON_MOVABLE(FakeReadbackManager)

  [[nodiscard]] auto CreateBufferReadback(std::string_view /*debug_name*/)
    -> std::shared_ptr<GpuBufferReadback> override
  {
    auto readback = std::make_shared<FakeBufferReadback>();
    buffer_readbacks.push_back(readback);
    return readback;
  }

  [[nodiscard]] auto CreateTextureReadback(std::string_view /*debug_name*/)
    -> std::shared_ptr<oxygen::graphics::GpuTextureReadback> override
  {
    return {};
  }

  auto Await(ReadbackTicket /*ticket*/)
    -> std::expected<oxygen::graphics::ReadbackResult, ReadbackError> override
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

  auto ReadBufferNow(const oxygen::graphics::Buffer& /*source*/,
    oxygen::graphics::BufferRange /*range*/)
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

  auto OnFrameStart(oxygen::frame::Slot /*slot*/) -> void override { }

  auto Shutdown(std::chrono::milliseconds /*timeout*/)
    -> std::expected<void, ReadbackError> override
  {
    return {};
  }

  std::vector<std::shared_ptr<FakeBufferReadback>> buffer_readbacks;
};

auto MakeConfig(FakeGraphics& graphics) -> RendererConfig
{
  auto config = RendererConfig {};
  config.upload_queue_key = graphics.QueueKeyFor(QueueRole::kGraphics).get();
  return config;
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Renderer>
{
  return {
    new Renderer(std::weak_ptr<Graphics>(graphics), MakeConfig(*graphics),
      RendererCapabilityFamily::kScenePreparation
        | RendererCapabilityFamily::kDeferredShading),
    [](Renderer* renderer) -> void {
      if (renderer != nullptr) {
        renderer->OnShutdown();
        std::default_delete<Renderer> {}(renderer);
      }
    },
  };
}

struct OcclusionModuleFixture {
  std::shared_ptr<FakeGraphics> graphics {
    std::make_shared<FakeGraphics>(),
  };
  std::shared_ptr<Renderer> renderer;
  std::unique_ptr<SceneTextures> scene_textures;

  OcclusionModuleFixture()
  {
    graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer = MakeRenderer(graphics);
    scene_textures = std::make_unique<SceneTextures>(*graphics,
      SceneTexturesConfig {
        .extent = { 16U, 16U },
      });
  }
};

//! A prepared frame of `draw_count` draws with published bindless slots.
struct TestScene {
  std::vector<oxygen::vortex::DrawMetadata> metadata;
  std::vector<PreparedSceneFrame::DrawSource> sources;
  PreparedSceneFrame frame;

  explicit TestScene(const std::uint32_t draw_count)
    : metadata(draw_count)
  {
    for (std::uint32_t draw = 0U; draw < draw_count; ++draw) {
      sources.push_back(PreparedSceneFrame::DrawSource {
        .node = oxygen::scene::NodeHandle { draw, 1U },
        .lod_index = oxygen::data::LodIndex {},
        .submesh_index = oxygen::data::SubmeshIndex {},
        .mesh_view_index = oxygen::data::MeshViewIndex {},
      });
    }
    frame.draw_metadata_bytes = std::as_bytes(std::span { metadata });
    frame.draw_sources = sources;
    frame.bindless_draw_metadata_slot = oxygen::ShaderVisibleIndex { 1U };
    frame.bindless_draw_cull_records_slot = oxygen::ShaderVisibleIndex { 2U };
    frame.bindless_worlds_slot = oxygen::ShaderVisibleIndex { 3U };
  }
};

auto MakeView(const bool reverse_z) -> ResolvedView
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = { .width = 16.0F, .height = 16.0F };
  params.view_config.scissor = { .right = 16, .bottom = 16 };
  params.view_config.reverse_z = reverse_z;
  return ResolvedView { params };
}

//! Drives frames of one module through both phases.
struct OcclusionFrameDriver {
  // Outlives the graphics, which shuts readbacks down when it stops.
  FakeReadbackManager readbacks;
  OcclusionModuleFixture fixture;
  OcclusionModule module {
    *fixture.renderer,
    OcclusionConfig {
      .enabled = true,
    },
  };
  ResolvedView view { MakeView(true) };
  TestScene scene { 3U };
  std::uint64_t frame_sequence { 0U };

  OcclusionFrameDriver()
  {
    fixture.graphics->readback_manager_
      = oxygen::observer_ptr<oxygen::graphics::ReadbackManager> {
          &readbacks,
        };
  }

  //! Records phase 1 and, when needed, phase 2 without a pyramid.
  auto Render(const ViewId view_id) -> RenderContext
  {
    auto ctx = RenderContext {};
    ctx.frame_sequence = oxygen::frame::SequenceNumber { ++frame_sequence };
    ctx.frame_slot = oxygen::frame::Slot { 0U };
    ctx.current_view.view_id = view_id;
    ctx.current_view.prepared_frame
      = oxygen::observer_ptr<const PreparedSceneFrame> { &scene.frame };
    ctx.current_view.resolved_view
      = oxygen::observer_ptr<const ResolvedView> { &view };
    auto recorder = fixture.graphics->AcquireCommandRecorder(
      fixture.graphics->QueueKeyFor(QueueRole::kGraphics), "Test stage");
    module.BuildPhase1(ctx, *recorder, *fixture.scene_textures);
    if (module.NeedsPhase2(ctx)) {
      module.BuildPhase2(ctx, *recorder, std::nullopt);
    }
    return ctx;
  }

  [[nodiscard]] auto DispatchedEntryPoints() const -> std::vector<std::string>
  {
    auto entry_points = std::vector<std::string> {};
    for (const auto& bind : fixture.graphics->compute_pipeline_log_.binds) {
      entry_points.push_back(bind.desc.ComputeShader().entry_point);
    }
    return entry_points;
  }
};

//! Without a prepared frame nothing is culled and no list filters.
NOLINT_TEST(OcclusionModuleTest, ViewWithoutPreparedFramePublishesNoVisibility)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule {
    *fixture.renderer,
    OcclusionConfig {
      .enabled = true,
    },
  };
  auto ctx = RenderContext {};
  auto recorder = fixture.graphics->AcquireCommandRecorder(
    fixture.graphics->QueueKeyFor(QueueRole::kGraphics), "Test stage");

  module.BuildPhase1(ctx, *recorder, *fixture.scene_textures);

  EXPECT_FALSE(ctx.current_view.draw_visibility.IsValid());
  EXPECT_FALSE(module.NeedsPhase2(ctx));
  EXPECT_TRUE(fixture.graphics->dispatch_log_.dispatches.empty());
}

//! Occlusion on records phase 1 and phase 2 and leaves the visibility final.
NOLINT_TEST(OcclusionModuleTest, EnabledViewRecordsBothPhases)
{
  auto driver = OcclusionFrameDriver {};

  const auto ctx = driver.Render(ViewId { 1U });

  EXPECT_TRUE(ctx.current_view.draw_visibility.IsValid());
  EXPECT_TRUE(ctx.current_view.draw_visibility.phase2);
  EXPECT_EQ(ctx.current_view.draw_visibility.draw_count, 3U);
  EXPECT_EQ(driver.DispatchedEntryPoints(),
    (std::vector<std::string> { "VortexOcclusionStatsClearCS",
      "VortexOcclusionPhase1CS", "VortexOcclusionPhase2CS" }));
}

//! Occlusion off, or a view without reversed-Z depth, culls by frustum in
//! phase 1 only.
NOLINT_TEST(OcclusionModuleTest, FrustumOnlyViewsSkipPhase2)
{
  for (const bool enabled : { false, true }) {
    SCOPED_TRACE(enabled);
    auto driver = OcclusionFrameDriver {};
    driver.module.SetConfig(OcclusionConfig { .enabled = enabled });
    driver.view = MakeView(!enabled);

    const auto ctx = driver.Render(ViewId { 1U });

    EXPECT_TRUE(ctx.current_view.draw_visibility.IsValid());
    EXPECT_FALSE(ctx.current_view.draw_visibility.phase2);
    EXPECT_FALSE(driver.module.NeedsPhase2(ctx));
    EXPECT_EQ(driver.DispatchedEntryPoints(),
      (std::vector<std::string> {
        "VortexOcclusionStatsClearCS", "VortexOcclusionPhase1CS" }));
  }
}

//! Counters read back frames later reach the view they were counted for,
//! with the frame they describe; a removed view forgets them.
NOLINT_TEST(OcclusionModuleTest, CountersReachTheirViewFramesLater)
{
  auto driver = OcclusionFrameDriver {};
  const auto pane = ViewId { 1U };
  const auto preview = ViewId { 2U };

  (void)driver.Render(pane);
  (void)driver.Render(preview);
  ASSERT_EQ(driver.readbacks.buffer_readbacks.size(), 2U);
  driver.readbacks.buffer_readbacks.front()->Complete(
    { 3U, 3U, 0U, 3U, 3U, 0U, 0U, 0U });
  EXPECT_FALSE(driver.module.GetStats(pane).has_value());

  (void)driver.Render(pane);

  const auto stats = driver.module.GetStats(pane);
  if (!stats.has_value()) {
    FAIL() << "Expected read-back counters";
  }
  EXPECT_EQ(stats.value().frame_sequence, oxygen::frame::SequenceNumber { 1U });
  EXPECT_TRUE(stats.value().occlusion_enabled);
  EXPECT_EQ(stats.value().counters.draw_count, 3U);
  EXPECT_EQ(stats.value().counters.phase1_drawn_count, 3U);
  EXPECT_FALSE(driver.module.GetStats(preview).has_value());

  driver.module.RemoveViewState(pane);
  EXPECT_FALSE(driver.module.GetStats(pane).has_value());
}

//! The slot word marks fresh slots and keeps "no slot" distinct.
NOLINT_TEST(OcclusionModuleTest, SlotWordsEncodeFreshSlots)
{
  using Assignment = HistorySlotAllocator::Assignment;
  EXPECT_EQ((Assignment { .slot = 5U, .fresh = false }).ToSlotWord(), 5U);
  EXPECT_EQ((Assignment { .slot = 5U, .fresh = true }).ToSlotWord(),
    5U | kFreshHistorySlotBit);
  EXPECT_EQ((Assignment { .slot = kNoHistorySlot, .fresh = true }).ToSlotWord(),
    kNoHistorySlot);
}

} // namespace
