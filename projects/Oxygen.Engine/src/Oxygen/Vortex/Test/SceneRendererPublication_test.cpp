//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

#include "Fakes/Graphics.h"

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

using oxygen::Format;
using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::FrameContext;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::scene::Scene;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneRenderer;
using oxygen::vortex::SceneTextureBindings;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::ShadingMode;
using oxygen::vortex::ViewFrameBindings;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::RendererPublicationProbe;
using oxygen::co::testing::TestEventLoop;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto RunRenderAsync(std::shared_ptr<Renderer> renderer, FrameContext* frame_context)
  -> oxygen::co::Co<>
{
  co_await renderer->OnRender(
    oxygen::observer_ptr<FrameContext> { frame_context });
}

auto MakeResolvedView(const float width, const float height) -> ResolvedView
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.view_matrix = glm::mat4(1.0F);
  params.proj_matrix = glm::mat4(1.0F);
  params.near_plane = 0.1F;
  params.far_plane = 1000.0F;
  return ResolvedView(params);
}

class SceneRendererPublicationTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    constexpr CapabilitySet kDeferredPublicationCapabilities
      = RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading;
    renderer_
      = { new Renderer(std::weak_ptr<Graphics>(graphics_), std::move(config),
            kDeferredPublicationCapabilities),
      DestroyRenderer };
  }

  [[nodiscard]] auto MakeFramebuffer(std::string_view debug_name) const
    -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = TextureDesc {};
    color_desc.width = 64U;
    color_desc.height = 64U;
    color_desc.format = Format::kRGBA8UNorm;
    color_desc.texture_type = TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = std::string(debug_name);

    auto color = graphics_->CreateTexture(color_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  static auto PrepareFrameContext(FrameContext& frame_context,
    const oxygen::frame::SequenceNumber frame_number,
    const oxygen::frame::Slot frame_slot) -> void
  {
    frame_context.SetCurrentPhase(oxygen::core::PhaseId::kFrameStart,
      oxygen::engine::internal::EngineTagFactory::Get());
    frame_context.SetFrameSequenceNumber(
      frame_number, oxygen::engine::internal::EngineTagFactory::Get());
    frame_context.SetFrameSlot(
      frame_slot, oxygen::engine::internal::EngineTagFactory::Get());

    auto timing = oxygen::engine::ModuleTimingData {};
    timing.game_delta_time = oxygen::time::SimulationClock::kMinDeltaTime;
    frame_context.SetModuleTimingData(
      timing, oxygen::engine::internal::EngineTagFactory::Get());
  }

  [[nodiscard]] static auto MakeView(
    const oxygen::observer_ptr<Framebuffer> framebuffer, const float width,
    const float height, const bool is_scene_view) -> oxygen::engine::ViewContext
  {
    auto view = oxygen::engine::ViewContext {};
    view.view.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view.metadata.name = is_scene_view ? "SceneView" : "OverlayView";
    view.metadata.purpose = is_scene_view ? "scene" : "overlay";
    view.metadata.is_scene_view = is_scene_view;
    view.render_target = framebuffer;
    return view;
  }

  auto RunRendererFrame(FrameContext& frame_context) const -> void
  {
    renderer_->OnFrameStart(oxygen::observer_ptr<FrameContext> { &frame_context });

    auto loop = TestEventLoop {};
    oxygen::co::Run(loop, RunRenderAsync(renderer_, &frame_context));
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(SceneRendererPublicationTest,
  RendererPublishesSceneTexturesThroughViewFrameBindingsAndViewConstants)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.PublishedBindings", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  auto framebuffer = MakeFramebuffer("SceneRendererPublicationTest.SceneColor");
  static_cast<void>(frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { framebuffer.get() }, 64.0F, 64.0F, true)));

  RunRendererFrame(frame_context);

  auto* scene_renderer = RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  const auto& published_bindings = scene_renderer->GetPublishedViewFrameBindings();
  const auto& scene_texture_bindings = scene_renderer->GetSceneTextureBindings();

  EXPECT_EQ(
    RendererPublicationProbe::GetViewConstants(*renderer_).GetBindlessViewFrameBindingsSlot().value,
    scene_renderer->GetPublishedViewFrameBindingsSlot());
  EXPECT_NE(scene_renderer->GetPublishedViewFrameBindingsSlot(),
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(published_bindings.scene_texture_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(published_bindings.draw_frame_slot, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    published_bindings.view_color_frame_slot, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(RendererPublicationProbe::GetViewConstantsManager(*renderer_), nullptr);
  EXPECT_NE(scene_renderer->GetPublishedViewId(), oxygen::kInvalidViewId);
  EXPECT_EQ(scene_texture_bindings.scene_color_srv,
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_texture_bindings.scene_color_uav,
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_texture_bindings.scene_depth_srv,
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_texture_bindings.partial_depth_srv,
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_texture_bindings.velocity_srv,
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_texture_bindings.gbuffer_srvs[0],
    SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  FrameStartResizeInvalidatesPublicationUntilTheNextRenderRefresh)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Resize", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  auto framebuffer = MakeFramebuffer("SceneRendererPublicationTest.Resize");
  const auto view_id = frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { framebuffer.get() }, 64.0F, 64.0F, true));

  RunRendererFrame(frame_context);

  auto* scene_renderer = RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  ASSERT_NE(scene_renderer->GetPublishedViewFrameBindingsSlot(),
    oxygen::kInvalidShaderVisibleIndex);

  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 2U }, oxygen::frame::Slot { 1U });
  auto resized_view
    = MakeView(oxygen::observer_ptr { framebuffer.get() }, 128.0F, 72.0F, true);
  resized_view.id = view_id;
  frame_context.UpdateView(view_id, resized_view);

  renderer_->OnFrameStart(oxygen::observer_ptr<FrameContext> { &frame_context });

  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent(), (glm::uvec2 { 128U, 72U }));
  EXPECT_EQ(scene_renderer->GetPublishedViewFrameBindingsSlot(),
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(scene_renderer->GetSceneTextureBindings().scene_color_srv,
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_renderer->GetSceneTextureBindings().valid_flags, 0U);

  auto loop = TestEventLoop {};
  oxygen::co::Run(loop, RunRenderAsync(renderer_, &frame_context));

  EXPECT_NE(scene_renderer->GetPublishedViewFrameBindingsSlot(),
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(scene_renderer->GetPublishedViewFrameBindings().scene_texture_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  ResolveAndCleanupPublishExplicitArtifactsInsteadOfLiveAttachments)
{
  auto config = SceneTexturesConfig {
    .extent = { 96U, 54U },
    .enable_velocity = false,
    .enable_custom_depth = false,
    .gbuffer_count = 4U,
    .msaa_sample_count = 1U,
  };
  auto scene_renderer = SceneRenderer(
    *renderer_, *graphics_, config, ShadingMode::kDeferred);
  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.ResolveAndCleanup", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });
  auto framebuffer = MakeFramebuffer("SceneRendererPublicationTest.ResolveAndCleanup");
  const auto view_id = frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { framebuffer.get() }, 96.0F, 54.0F, true));
  auto resolved_view = MakeResolvedView(96.0F, 54.0F);

  auto render_context = RenderContext {};
  render_context.scene = oxygen::observer_ptr<Scene> { scene.get() };
  render_context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  render_context.active_view_index = std::size_t { 0U };
  render_context.frame_views.push_back({
    .view_id = view_id,
    .is_scene_view = true,
    .composition_view = {},
    .shading_mode_override = {},
    .resolved_view = oxygen::observer_ptr<const ResolvedView> { &resolved_view },
    .primary_target = oxygen::observer_ptr<Framebuffer> { framebuffer.get() },
  });
  render_context.current_view.view_id = view_id;
  render_context.current_view.exposure_view_id = view_id;
  render_context.current_view.resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &resolved_view };

  scene_renderer.OnFrameStart(frame_context);
  scene_renderer.OnRender(render_context);

  const auto& bindings = scene_renderer.GetSceneTextureBindings();
  const auto& extracts = scene_renderer.GetSceneTextureExtracts();

  EXPECT_EQ(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.custom_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.custom_stencil_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, bindings.scene_color_srv);
  EXPECT_NE(bindings.scene_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_TRUE(extracts.resolved_scene_color.valid);
  EXPECT_NE(extracts.resolved_scene_color.texture,
    &scene_renderer.GetSceneTextures().GetSceneColor());
  EXPECT_TRUE(extracts.resolved_scene_depth.valid);
  EXPECT_NE(extracts.resolved_scene_depth.texture, nullptr);
  EXPECT_TRUE(extracts.prev_scene_depth.valid);
  EXPECT_NE(extracts.prev_scene_depth.texture, nullptr);
  EXPECT_FALSE(extracts.prev_velocity.valid);
  EXPECT_EQ(extracts.prev_velocity.texture, nullptr);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  StageMilestonesRefreshBindingsProgressivelyInsteadOfAllAtOnce)
{
  auto config = SceneTexturesConfig {
    .extent = { 96U, 54U },
    .enable_velocity = true,
    .enable_custom_depth = true,
    .gbuffer_count = 4U,
    .msaa_sample_count = 1U,
  };
  auto scene_renderer = SceneRenderer(
    *renderer_, *graphics_, config, ShadingMode::kDeferred);

  scene_renderer.ApplyStage3DepthPrepassState();
  auto bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);

  scene_renderer.ApplyStage9BasePassState();
  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[1], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[2], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[3], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);

  scene_renderer.ApplyStage10RebuildState();
  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.stencil_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[1], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[2], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[3], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, bindings.scene_color_srv);

  scene_renderer.ApplyStage22PostProcessState();
  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.custom_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.custom_stencil_srv, SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  FinalVelocityBindingStaysValidAfterStage9AndStage10)
{
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);
  scene_renderer.ApplyStage9BasePassState();

  auto bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);

  scene_renderer.ApplyStage10RebuildState();

  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  RenderContextViewStateMaterializesAllEligibleViewsBeforeSelectingTheCursor)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });

  auto overlay_fb = MakeFramebuffer("SceneRendererPublicationTest.Overlay");
  auto scene_fb_small = MakeFramebuffer("SceneRendererPublicationTest.SceneSmall");
  auto scene_fb_large = MakeFramebuffer("SceneRendererPublicationTest.SceneLarge");

  static_cast<void>(frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { overlay_fb.get() }, 48.0F, 48.0F, false)));
  const auto first_scene_id = frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { scene_fb_small.get() }, 64.0F, 64.0F, true));
  static_cast<void>(frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { scene_fb_large.get() }, 128.0F, 72.0F, true)));

  auto render_context = RenderContext {};
  RendererPublicationProbe::PopulateRenderContextViewState(
    *renderer_, render_context, frame_context, false);

  ASSERT_EQ(render_context.frame_views.size(), 3U);
  ASSERT_NE(render_context.GetActiveViewEntry(), nullptr);
  EXPECT_EQ(render_context.current_view.view_id, first_scene_id);
  EXPECT_EQ(render_context.current_view.prepared_frame.get(), nullptr);
  EXPECT_EQ(render_context.GetActiveViewEntry()->view_id, first_scene_id);
  EXPECT_TRUE(render_context.GetActiveViewEntry()->is_scene_view);
  EXPECT_EQ(render_context.pass_target.get(),
    render_context.GetActiveViewEntry()->primary_target.get());
}

} // namespace
