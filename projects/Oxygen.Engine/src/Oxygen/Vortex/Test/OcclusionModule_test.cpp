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
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
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
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::graphics::QueueRole;
using oxygen::vortex::OcclusionConfig;
using oxygen::vortex::OcclusionFallbackReason;
using oxygen::vortex::OcclusionModule;
using oxygen::vortex::PreparedSceneFrame;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
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

auto MakePreparedFrame(std::vector<oxygen::vortex::DrawMetadata>& metadata)
  -> PreparedSceneFrame
{
  auto frame = PreparedSceneFrame {};
  frame.draw_metadata_bytes = std::as_bytes(std::span {
    metadata,
  });
  return frame;
}

auto MakePreparedFrame(std::vector<oxygen::vortex::DrawMetadata>& metadata,
  std::vector<glm::vec4>& bounds) -> PreparedSceneFrame
{
  auto frame = MakePreparedFrame(metadata);
  frame.draw_bounding_spheres = std::span<const glm::vec4> {
    bounds,
  };
  return frame;
}

auto MakePreparedFrame(std::vector<oxygen::vortex::DrawMetadata>& metadata,
  std::vector<glm::vec4>& bounds,
  std::vector<PreparedSceneFrame::DrawSource>& sources) -> PreparedSceneFrame
{
  auto frame = MakePreparedFrame(metadata, bounds);
  frame.draw_sources = std::span<const PreparedSceneFrame::DrawSource> {
    sources,
  };
  return frame;
}

auto DrawOf(const std::uint32_t node_index) -> PreparedSceneFrame::DrawSource
{
  return PreparedSceneFrame::DrawSource {
    .node = oxygen::scene::NodeHandle { node_index, 1U },
    .submesh_index = oxygen::data::SubmeshIndex {},
  };
}

//! Drives frames of one module against a current furthest HZB.
struct OcclusionFrameDriver {
  // Outlives the graphics, which shuts readbacks down when it stops.
  FakeReadbackManager readbacks;
  OcclusionModuleFixture fixture;
  std::shared_ptr<oxygen::graphics::Texture> hzb_texture;
  OcclusionModule module {
    *fixture.renderer,
    OcclusionConfig {
      .enabled = true,
    },
  };
  std::uint64_t frame_sequence { 0U };

  OcclusionFrameDriver()
  {
    fixture.graphics->readback_manager_
      = oxygen::observer_ptr<oxygen::graphics::ReadbackManager> {
          &readbacks,
        };
    hzb_texture
      = fixture.graphics->CreateTexture(oxygen::graphics::TextureDesc {
        .width = 8U,
        .height = 8U,
        .format = oxygen::Format::kR32Float,
        .texture_type = oxygen::TextureType::kTexture2D,
        .debug_name = "OcclusionModuleTest.Hzb",
        .is_shader_resource = true,
        .clear_value = {},
      });
  }

  //! Renders one frame of a view and returns its published visibility.
  auto Render(const oxygen::ViewId view,
    std::vector<PreparedSceneFrame::DrawSource> sources) -> std::vector<bool>
  {
    auto metadata = std::vector<oxygen::vortex::DrawMetadata>(sources.size());
    auto bounds = std::vector<glm::vec4>(
      sources.size(), glm::vec4 { 0.0F, 0.0F, 5.0F, 1.0F });
    auto prepared_frame = MakePreparedFrame(metadata, bounds, sources);
    auto ctx = RenderContext {};
    ctx.frame_sequence = oxygen::frame::SequenceNumber { ++frame_sequence };
    ctx.frame_slot = oxygen::frame::Slot { 0U };
    ctx.current_view.view_id = view;
    ctx.current_view.prepared_frame
      = oxygen::observer_ptr<const PreparedSceneFrame> { &prepared_frame };
    ctx.current_view.screen_hzb_available = true;
    ctx.current_view.screen_hzb_furthest_texture
      = oxygen::observer_ptr<const oxygen::graphics::Texture> {
          hzb_texture.get(),
        };
    ctx.current_view.screen_hzb_frame_slot = oxygen::ShaderVisibleIndex {
      123U,
    };

    module.Execute(ctx,
      *fixture.graphics->AcquireCommandRecorder(
        fixture.graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
        "Test stage"),
      *fixture.scene_textures);

    const auto& results = module.GetCurrentResults();
    auto visible = std::vector<bool> {};
    visible.reserve(sources.size());
    for (std::uint32_t draw = 0U; draw < sources.size(); ++draw) {
      visible.push_back(results.IsDrawVisible(draw));
    }
    return visible;
  }
};

NOLINT_TEST(OcclusionTypesTest, FallbackReasonStringsAreStable)
{
  EXPECT_EQ(oxygen::vortex::to_string(OcclusionFallbackReason::kNone), "None");
  EXPECT_EQ(
    oxygen::vortex::to_string(OcclusionFallbackReason::kNoCurrentFurthestHzb),
    "NoCurrentFurthestHzb");
  EXPECT_EQ(
    oxygen::vortex::to_string(OcclusionFallbackReason::kCapacityOverflow),
    "CapacityOverflow");
}

NOLINT_TEST(OcclusionTypesTest, InvalidResultsAreConservativelyVisible)
{
  const auto results = oxygen::vortex::MakeInvalidOcclusionFrameResults(
    OcclusionFallbackReason::kStageDisabled);

  EXPECT_FALSE(results.valid);
  EXPECT_TRUE(results.IsDrawVisible(0U));
  EXPECT_TRUE(results.IsDrawVisible(99U));
  EXPECT_EQ(results.fallback_reason, OcclusionFallbackReason::kStageDisabled);
}

NOLINT_TEST(OcclusionModuleTest, DisabledStagePublishesInvalidVisibleFallback)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule {
    *fixture.renderer,
  };
  auto ctx = RenderContext {};

  module.Execute(ctx,
    *fixture.graphics->AcquireCommandRecorder(
      fixture.graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
      "Test stage"),
    *fixture.scene_textures);

  ASSERT_NE(ctx.current_view.occlusion_results.get(), nullptr);
  EXPECT_FALSE(ctx.current_view.occlusion_results->valid);
  EXPECT_TRUE(ctx.current_view.occlusion_results->IsDrawVisible(7U));
  EXPECT_EQ(
    module.GetStats().fallback_reason, OcclusionFallbackReason::kStageDisabled);
}

NOLINT_TEST(OcclusionModuleTest, EnabledStageWithoutPreparedFrameStaysInvalid)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule {
    *fixture.renderer,
    OcclusionConfig {
      .enabled = true,
    },
  };
  auto ctx = RenderContext {};

  module.Execute(ctx,
    *fixture.graphics->AcquireCommandRecorder(
      fixture.graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
      "Test stage"),
    *fixture.scene_textures);

  ASSERT_NE(ctx.current_view.occlusion_results.get(), nullptr);
  EXPECT_FALSE(ctx.current_view.occlusion_results->valid);
  EXPECT_EQ(module.GetStats().fallback_reason,
    OcclusionFallbackReason::kNoPreparedFrame);
}

NOLINT_TEST(OcclusionModuleTest, MissingCurrentFurthestHzbPublishesAllVisible)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule {
    *fixture.renderer,
    OcclusionConfig {
      .enabled = true,
    },
  };
  auto metadata = std::vector<oxygen::vortex::DrawMetadata>(3U);
  auto prepared_frame = MakePreparedFrame(metadata);
  auto ctx = RenderContext {};
  ctx.current_view.prepared_frame
    = oxygen::observer_ptr<const PreparedSceneFrame> {
        &prepared_frame,
      };

  module.Execute(ctx,
    *fixture.graphics->AcquireCommandRecorder(
      fixture.graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
      "Test stage"),
    *fixture.scene_textures);

  const auto& results = module.GetCurrentResults();
  EXPECT_TRUE(results.valid);
  EXPECT_EQ(results.draw_count, 3U);
  EXPECT_EQ(results.visible_by_draw.size(), 3U);
  EXPECT_TRUE(results.IsDrawVisible(0U));
  EXPECT_TRUE(results.IsDrawVisible(2U));
  EXPECT_TRUE(results.IsDrawVisible(3U));
  EXPECT_EQ(
    results.fallback_reason, OcclusionFallbackReason::kNoCurrentFurthestHzb);
  EXPECT_EQ(module.GetStats().visible_count, 3U);
  EXPECT_EQ(module.GetStats().occluded_count, 0U);
  EXPECT_FALSE(module.GetStats().current_furthest_hzb_available);
  ASSERT_NE(ctx.current_view.occlusion_results.get(), nullptr);
  EXPECT_EQ(ctx.current_view.occlusion_results.get(), &results);
}

NOLINT_TEST(OcclusionModuleTest,
  CurrentHzbSubmitsGpuTestWhilePublishingConservativeFallback)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule {
    *fixture.renderer,
    OcclusionConfig {
      .enabled = true,
    },
  };
  auto metadata = std::vector<oxygen::vortex::DrawMetadata>(3U);
  auto sources = std::vector<PreparedSceneFrame::DrawSource> {
    DrawOf(1U),
    DrawOf(2U),
    DrawOf(3U),
  };
  auto bounds = std::vector<glm::vec4> {
    {
      0.0F,
      0.0F,
      5.0F,
      1.0F,
    },
    {
      2.0F,
      0.0F,
      8.0F,
      1.0F,
    },
    {
      -2.0F,
      0.0F,
      8.0F,
      1.0F,
    },
  };
  auto prepared_frame = MakePreparedFrame(metadata, bounds, sources);
  auto hzb_texture
    = fixture.graphics->CreateTexture(oxygen::graphics::TextureDesc {
      .width = 8U,
      .height = 8U,
      .format = oxygen::Format::kR32Float,
      .texture_type = oxygen::TextureType::kTexture2D,
      .debug_name = "OcclusionModuleTest.Hzb",
      .is_shader_resource = true,
      .clear_value = {},
    });

  auto ctx = RenderContext {};
  ctx.frame_sequence = oxygen::frame::SequenceNumber {
    1U,
  };
  ctx.frame_slot = oxygen::frame::Slot {
    0U,
  };
  ctx.current_view.prepared_frame
    = oxygen::observer_ptr<const PreparedSceneFrame> {
        &prepared_frame,
      };
  ctx.current_view.screen_hzb_available = true;
  ctx.current_view.screen_hzb_furthest_texture
    = oxygen::observer_ptr<const oxygen::graphics::Texture> {
        hzb_texture.get(),
      };
  ctx.current_view.screen_hzb_frame_slot = oxygen::ShaderVisibleIndex {
    123U,
  };

  module.Execute(ctx,
    *fixture.graphics->AcquireCommandRecorder(
      fixture.graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
      "Test stage"),
    *fixture.scene_textures);

  const auto& results = module.GetCurrentResults();
  EXPECT_TRUE(results.valid);
  EXPECT_TRUE(results.IsDrawVisible(0U));
  EXPECT_EQ(
    results.fallback_reason, OcclusionFallbackReason::kReadbackUnavailable);
  EXPECT_EQ(module.GetStats().candidate_count, 3U);
  EXPECT_EQ(module.GetStats().submitted_count, 3U);
  ASSERT_EQ(fixture.graphics->dispatch_log_.dispatches.size(), 1U);
  EXPECT_EQ(
    fixture.graphics->dispatch_log_.dispatches.front().thread_group_count_x,
    1U);
  ASSERT_FALSE(fixture.graphics->compute_pipeline_log_.binds.empty());
  EXPECT_EQ(fixture.graphics->compute_pipeline_log_.binds.front()
              .desc.ComputeShader()
              .source_path,
    "Vortex/Stages/Occlusion/OcclusionTest.hlsl");
}

//! Draw indices follow each frame's sort order: a result read back after the
//! order changed must reach the draw that was tested, not the one now at its
//! index. A huge floor swapped with an occluded draw would otherwise vanish.
NOLINT_TEST(OcclusionModuleTest, ReadBackResultsFollowTheDrawSource)
{
  auto driver = OcclusionFrameDriver {};
  const auto view = oxygen::ViewId { 1U };
  const auto floor = DrawOf(1U);
  const auto hidden = DrawOf(2U);

  (void)driver.Render(view, { floor, hidden });
  ASSERT_EQ(driver.readbacks.buffer_readbacks.size(), 1U);
  driver.readbacks.buffer_readbacks.front()->Complete({ 1U, 0U });

  const auto visible = driver.Render(view, { hidden, floor });

  EXPECT_FALSE(visible.front());
  EXPECT_TRUE(visible.back());
}

//! A view's results never reach another view, such as a camera preview
//! composed over the pane.
NOLINT_TEST(OcclusionModuleTest, ReadBackResultsStayWithTheirView)
{
  auto driver = OcclusionFrameDriver {};
  const auto pane = oxygen::ViewId { 1U };
  const auto preview = oxygen::ViewId { 2U };
  const auto floor = DrawOf(1U);

  (void)driver.Render(preview, { floor });
  ASSERT_EQ(driver.readbacks.buffer_readbacks.size(), 1U);
  driver.readbacks.buffer_readbacks.front()->Complete({ 0U });

  EXPECT_TRUE(driver.Render(pane, { floor }).front());
  EXPECT_FALSE(driver.Render(preview, { floor }).front());
}

//! A removed view's pending results are never applied to a later view that
//! reuses its id.
NOLINT_TEST(OcclusionModuleTest, RemovedViewForgetsItsPendingResults)
{
  auto driver = OcclusionFrameDriver {};
  const auto view = oxygen::ViewId { 1U };
  const auto floor = DrawOf(1U);

  (void)driver.Render(view, { floor });
  ASSERT_EQ(driver.readbacks.buffer_readbacks.size(), 1U);
  driver.readbacks.buffer_readbacks.front()->Complete({ 0U });
  driver.module.RemoveViewState(view);

  EXPECT_TRUE(driver.Render(view, { floor }).front());
}

} // namespace
