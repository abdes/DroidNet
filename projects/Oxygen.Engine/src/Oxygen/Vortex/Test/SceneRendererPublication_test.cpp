//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>
#include <ranges>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ScreenHzbFrameBindings.h>
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
  using oxygen::vortex::CompositionView;
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

auto MakeResolvedView(const float width, const float height,
  const float top_left_x = 0.0F, const float top_left_y = 0.0F)
  -> ResolvedView
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = ViewPort {
    .top_left_x = top_left_x,
    .top_left_y = top_left_y,
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
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting;
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

  [[nodiscard]] auto MakeSceneRenderContext(const std::shared_ptr<Scene>& scene,
    const ViewId view_id, const ResolvedView& resolved_view,
    const oxygen::observer_ptr<Framebuffer> framebuffer) const -> RenderContext
  {
    auto render_context = RenderContext {};
    render_context.scene = oxygen::observer_ptr<Scene> { scene.get() };
    render_context.frame_slot = oxygen::frame::Slot { 0U };
    render_context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
    render_context.active_view_index = std::size_t { 0U };
    render_context.frame_views.push_back({
      .view_id = view_id,
      .is_scene_view = true,
      .composition_view = {},
      .shading_mode_override = {},
      .resolved_view = oxygen::observer_ptr<const ResolvedView> { &resolved_view },
      .primary_target = framebuffer,
    });
    render_context.current_view.view_id = view_id;
    render_context.current_view.exposure_view_id = view_id;
    render_context.current_view.resolved_view
      = oxygen::observer_ptr<const ResolvedView> { &resolved_view };
    render_context.view_constants = graphics_->CreateBuffer({
      .size_bytes = 1024U,
      .usage = oxygen::graphics::BufferUsage::kConstant,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = "SceneRendererPublicationTest.ViewConstants",
    });
    return render_context;
  }

  auto RunRendererFrame(FrameContext& frame_context) const -> void
  {
    renderer_->OnFrameStart(oxygen::observer_ptr<FrameContext> { &frame_context });

    auto loop = TestEventLoop {};
    oxygen::co::Run(loop, RunRenderAsync(renderer_, &frame_context));
    renderer_->OnFrameEnd(oxygen::observer_ptr<FrameContext> { &frame_context });
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(SceneRendererPublicationTest,
  RendererOnRenderSkipsGracefullyWhenNoViewsAreRegistered)
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
  constexpr CapabilitySet kDeferredPublicationWithPostProcessCapabilities
    = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kLightingData
    | RendererCapabilityFamily::kEnvironmentLighting
    | RendererCapabilityFamily::kFinalOutputComposition;
  auto renderer = std::shared_ptr<Renderer> {
    new Renderer(std::weak_ptr<Graphics>(graphics_), std::move(config),
      kDeferredPublicationWithPostProcessCapabilities),
    DestroyRenderer,
  };

  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.NoViews", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  renderer->OnFrameStart(oxygen::observer_ptr<FrameContext> { &frame_context });

  auto loop = TestEventLoop {};
  EXPECT_NO_THROW(oxygen::co::Run(loop, RunRenderAsync(renderer, &frame_context)));

  renderer->OnFrameEnd(oxygen::observer_ptr<FrameContext> { &frame_context });
}

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
  EXPECT_EQ(published_bindings.history_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(published_bindings.draw_frame_slot, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(
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
  EXPECT_EQ(scene_texture_bindings.gbuffer_srvs[4],
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_texture_bindings.gbuffer_srvs[5],
    SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  RendererPublishesLightingBindingsThroughViewFrameBindings)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.LightingBindings", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  auto framebuffer = MakeFramebuffer("SceneRendererPublicationTest.LightingBindings");
  static_cast<void>(frame_context.RegisterView(
    MakeView(oxygen::observer_ptr { framebuffer.get() }, 64.0F, 64.0F, true)));

  RunRendererFrame(frame_context);

  auto* scene_renderer = RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_NE(scene_renderer->GetPublishedViewFrameBindings().lighting_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
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
  render_context.frame_slot = oxygen::frame::Slot { 0U };
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
  render_context.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "SceneRendererPublicationTest.ViewConstants",
  });

  scene_renderer.OnFrameStart(frame_context);
  graphics_->texture_copy_log_.copies.clear();
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
  EXPECT_TRUE(std::ranges::any_of(graphics_->texture_copy_log_.copies,
    [&scene_renderer, &extracts](const auto& copy) -> bool {
      return copy.src == scene_renderer.GetSceneTextures().GetSceneColorResource().get()
        && copy.dst == extracts.resolved_scene_color.texture;
    }));
  EXPECT_TRUE(std::ranges::any_of(graphics_->texture_copy_log_.copies,
    [&scene_renderer, &extracts](const auto& copy) -> bool {
      return copy.src == scene_renderer.GetSceneTextures().GetSceneDepthResource().get()
        && copy.dst == extracts.resolved_scene_depth.texture;
    }));
  EXPECT_TRUE(extracts.prev_scene_depth.valid);
  EXPECT_NE(extracts.prev_scene_depth.texture, nullptr);
  EXPECT_TRUE(std::ranges::any_of(graphics_->texture_copy_log_.copies,
    [&extracts](const auto& copy) -> bool {
      return copy.src == extracts.resolved_scene_depth.texture
        && copy.dst == extracts.prev_scene_depth.texture;
    }));
  EXPECT_FALSE(extracts.prev_velocity.valid);
  EXPECT_EQ(extracts.prev_velocity.texture, nullptr);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  DestructorReleasesExtractArtifactsWithoutAnotherFrameStart)
{
  auto& registry = graphics_->GetResourceRegistry();
  const auto baseline_registered = registry.GetRegisteredResourceCount();
  auto post_render_registered = baseline_registered;

  {
    auto config = SceneTexturesConfig {
      .extent = { 96U, 54U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    };
    auto scene_renderer = SceneRenderer(
      *renderer_, *graphics_, config, ShadingMode::kDeferred);
    auto frame_context = FrameContext {};
    PrepareFrameContext(frame_context, oxygen::frame::SequenceNumber { 1U },
      oxygen::frame::Slot { 0U });
    auto scene = std::make_shared<Scene>(
      "SceneRendererPublicationTest.DestructorCleanup", 16U);
    frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });
    auto framebuffer
      = MakeFramebuffer("SceneRendererPublicationTest.DestructorCleanup");
    const auto view_id = frame_context.RegisterView(
      MakeView(oxygen::observer_ptr { framebuffer.get() }, 96.0F, 54.0F, true));
    auto resolved_view = MakeResolvedView(96.0F, 54.0F);

    auto render_context = RenderContext {};
    render_context.scene = oxygen::observer_ptr<Scene> { scene.get() };
    render_context.frame_slot = oxygen::frame::Slot { 0U };
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
    render_context.view_constants = graphics_->CreateBuffer({
      .size_bytes = 1024U,
      .usage = oxygen::graphics::BufferUsage::kConstant,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = "SceneRendererPublicationTest.DestructorCleanup.ViewConstants",
    });

    scene_renderer.OnFrameStart(frame_context);
    graphics_->texture_copy_log_.copies.clear();
    scene_renderer.OnRender(render_context);

    const auto& extracts = scene_renderer.GetSceneTextureExtracts();
    EXPECT_TRUE(extracts.resolved_scene_color.valid);
    EXPECT_TRUE(extracts.resolved_scene_depth.valid);
    EXPECT_TRUE(extracts.prev_scene_depth.valid);
    EXPECT_TRUE(extracts.prev_velocity.valid);
    EXPECT_TRUE(std::ranges::any_of(graphics_->texture_copy_log_.copies,
      [&extracts](const auto& copy) -> bool {
        return copy.src == extracts.resolved_scene_depth.texture
          && copy.dst == extracts.prev_scene_depth.texture;
      }));
    EXPECT_TRUE(std::ranges::any_of(graphics_->texture_copy_log_.copies,
      [&scene_renderer, &extracts](const auto& copy) -> bool {
        return copy.src == scene_renderer.GetSceneTextures().GetVelocity()
          && copy.dst == extracts.prev_velocity.texture;
      }));
    post_render_registered = registry.GetRegisteredResourceCount();
    EXPECT_GT(post_render_registered, baseline_registered);
  }

  // Extract artifacts are SceneRenderer-owned and must be released on
  // destruction. Renderer-owned publication state may legitimately persist
  // across SceneRenderer instances, so the count must drop but not necessarily
  // return exactly to the baseline.
  EXPECT_GE(registry.GetRegisteredResourceCount(), baseline_registered);
  EXPECT_LT(registry.GetRegisteredResourceCount(), post_render_registered);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage3PublicationKeepsSceneColorAndGBuffersInvalidUntilStage10)
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
  auto stage10_scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Stage3ToStage10", 16U);
  auto stage10_framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage3ToStage10");
  auto stage10_view = MakeResolvedView(96.0F, 54.0F);
  auto stage10_context = MakeSceneRenderContext(stage10_scene, ViewId { 7U },
    stage10_view,
    oxygen::observer_ptr { stage10_framebuffer.get() });

  scene_renderer.PublishDepthPrepassProducts();
  auto bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);

  scene_renderer.PublishBasePassVelocity();
  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[1], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[2], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[3], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[4], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[5], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);

  scene_renderer.PublishDeferredBasePassSceneTextures(stage10_context);
  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.stencil_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[1], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[2], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[3], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[4], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[5], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, bindings.scene_color_srv);
  EXPECT_NE(
    scene_renderer.GetPublishedViewFrameBindings().scene_texture_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);

  scene_renderer.PublishCustomDepthProducts();
  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.custom_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.custom_stencil_srv, SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive)
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
  auto stage10_scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Stage10Publication", 16U);
  auto stage10_framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage10Publication");
  auto stage10_view = MakeResolvedView(64.0F, 64.0F);
  auto stage10_context = MakeSceneRenderContext(stage10_scene, ViewId { 11U },
    stage10_view,
    oxygen::observer_ptr { stage10_framebuffer.get() });

  scene_renderer.PublishBasePassVelocity();

  auto bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);

  scene_renderer.PublishDeferredBasePassSceneTextures(stage10_context);

  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(
    scene_renderer.GetPublishedViewFrameBindings().scene_texture_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  SceneTexturesRebuildHelperAloneDoesNotPublishStage10Products)
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
  auto stage10_scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.RebuildHelper", 16U);
  auto stage10_framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.RebuildHelper");
  auto stage10_view = MakeResolvedView(64.0F, 64.0F);
  auto stage10_context = MakeSceneRenderContext(stage10_scene, ViewId { 8U },
    stage10_view,
    oxygen::observer_ptr { stage10_framebuffer.get() });

  scene_renderer.PublishDepthPrepassProducts();
  scene_renderer.PublishBasePassVelocity();

  scene_renderer.GetSceneTextures().RebuildWithGBuffers();

  auto bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.stencil_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[1], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[2], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[3], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[4], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[5], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(
    scene_renderer.GetPublishedViewFrameBindings().scene_texture_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);

  scene_renderer.PublishDeferredBasePassSceneTextures(stage10_context);

  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_color_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.stencil_srv, SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[0], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[1], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[2], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.gbuffer_srvs[3], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[4], SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[5], SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(
    scene_renderer.GetPublishedViewFrameBindings().scene_texture_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
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

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage5PublishesRealScreenHzbProductsForDeferredViews)
{
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig {
      .extent = { 96U, 54U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);

  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Stage5ScreenHzb", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  auto framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage5ScreenHzb");
  const auto view_id = ViewId { 11U };
  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto render_context = MakeSceneRenderContext(
    scene, view_id, resolved_view, oxygen::observer_ptr { framebuffer.get() });

  scene_renderer.OnFrameStart(frame_context);
  graphics_->dispatch_log_.dispatches.clear();
  graphics_->compute_pipeline_log_.binds.clear();
  graphics_->texture_copy_log_.copies.clear();
  scene_renderer.OnRender(render_context);

  const auto& screen_hzb = scene_renderer.GetPublishedScreenHzbBindings();
  EXPECT_NE(scene_renderer.GetPublishedViewFrameBindings().screen_hzb_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagAvailable)
    != 0U);
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagClosestValid)
    != 0U);
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagFurthestValid)
    != 0U);
  EXPECT_TRUE(screen_hzb.closest_srv.IsValid());
  EXPECT_TRUE(screen_hzb.furthest_srv.IsValid());
  EXPECT_EQ(screen_hzb.width, 64U);
  EXPECT_EQ(screen_hzb.height, 32U);
  EXPECT_EQ(screen_hzb.mip_count, 6U);
  EXPECT_TRUE(render_context.current_view.screen_hzb_available);
  ASSERT_NE(render_context.current_view.screen_hzb_closest_texture.get(), nullptr);
  ASSERT_NE(render_context.current_view.screen_hzb_furthest_texture.get(), nullptr);
  EXPECT_EQ(render_context.current_view.screen_hzb_width, screen_hzb.width);
  EXPECT_EQ(render_context.current_view.screen_hzb_height, screen_hzb.height);
  EXPECT_EQ(render_context.current_view.screen_hzb_mip_count, screen_hzb.mip_count);
  EXPECT_EQ(render_context.current_view.screen_hzb_closest_texture->GetDescriptor().width,
    screen_hzb.width);
  EXPECT_EQ(render_context.current_view.screen_hzb_closest_texture->GetDescriptor().height,
    screen_hzb.height);
  EXPECT_EQ(render_context.current_view.screen_hzb_closest_texture->GetDescriptor().mip_levels,
    screen_hzb.mip_count);
  EXPECT_EQ(render_context.current_view.screen_hzb_furthest_texture->GetDescriptor().width,
    screen_hzb.width);
  EXPECT_EQ(render_context.current_view.screen_hzb_furthest_texture->GetDescriptor().height,
    screen_hzb.height);
  EXPECT_EQ(render_context.current_view.screen_hzb_furthest_texture->GetDescriptor().mip_levels,
    screen_hzb.mip_count);
  EXPECT_GE(graphics_->dispatch_log_.dispatches.size(), screen_hzb.mip_count);
  EXPECT_TRUE(std::ranges::any_of(graphics_->compute_pipeline_log_.binds,
    [](const auto& bind) -> bool {
      return bind.desc.GetName() == "Vortex.Stage5.ScreenHzbBuild"
        && bind.desc.ComputeShader().source_path
          == "Vortex/Stages/Hzb/ScreenHzbBuild.hlsl"
        && bind.desc.ComputeShader().entry_point == "VortexScreenHzbBuildCS";
    }));
  EXPECT_GE(graphics_->texture_copy_log_.copies.size(), screen_hzb.mip_count * 2U);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage5SubViewportHzbCarriesViewRectMapping)
{
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig {
      .extent = { 192U, 108U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);

  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 3U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Stage5SubViewport", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  auto framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage5SubViewport");
  const auto view_id = ViewId { 21U };
  auto resolved_view = MakeResolvedView(128.0F, 72.0F, 32.0F, 16.0F);
  auto render_context = MakeSceneRenderContext(
    scene, view_id, resolved_view, oxygen::observer_ptr { framebuffer.get() });
  render_context.frame_sequence = oxygen::frame::SequenceNumber { 3U };
  render_context.frame_slot = oxygen::frame::Slot { 0U };

  scene_renderer.OnFrameStart(frame_context);
  scene_renderer.OnRender(render_context);

  const auto& screen_hzb = scene_renderer.GetPublishedScreenHzbBindings();
  EXPECT_TRUE(render_context.current_view.screen_hzb_available);
  EXPECT_EQ(screen_hzb.width, 64U);
  EXPECT_EQ(screen_hzb.height, 64U);
  EXPECT_EQ(screen_hzb.mip_count, 6U);
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagAvailable)
    != 0U);
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagClosestValid)
    != 0U);
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagFurthestValid)
    != 0U);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_size_x, 64.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_size_y, 64.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_view_size_x, 128.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_view_size_y, 72.0F);
  EXPECT_EQ(screen_hzb.hzb_view_rect_min_x, 0);
  EXPECT_EQ(screen_hzb.hzb_view_rect_min_y, 0);
  EXPECT_EQ(screen_hzb.hzb_view_rect_width, 128);
  EXPECT_EQ(screen_hzb.hzb_view_rect_height, 72);
  EXPECT_FLOAT_EQ(screen_hzb.viewport_uv_to_hzb_buffer_uv_x, 1.0F);
  EXPECT_FLOAT_EQ(screen_hzb.viewport_uv_to_hzb_buffer_uv_y, 72.0F / 128.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_uv_factor_x, 1.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_uv_factor_y, 72.0F / 128.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_uv_inv_factor_x, 1.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_uv_inv_factor_y, 16.0F / 9.0F);
  EXPECT_NEAR(screen_hzb.hzb_uv_to_screen_uv_scale_x, 2.0F / 3.0F, 1.0e-6F);
  EXPECT_NEAR(screen_hzb.hzb_uv_to_screen_uv_scale_y, 32.0F / 27.0F, 1.0e-6F);
  EXPECT_NEAR(screen_hzb.hzb_uv_to_screen_uv_bias_x, 1.0F / 6.0F, 1.0e-6F);
  EXPECT_NEAR(screen_hzb.hzb_uv_to_screen_uv_bias_y, 4.0F / 27.0F, 1.0e-6F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_base_texel_size_x, 1.0F / 64.0F);
  EXPECT_FLOAT_EQ(screen_hzb.hzb_base_texel_size_y, 1.0F / 64.0F);
  EXPECT_FLOAT_EQ(screen_hzb.sample_pixel_to_hzb_uv_x, 1.0F / 128.0F);
  EXPECT_FLOAT_EQ(screen_hzb.sample_pixel_to_hzb_uv_y, 1.0F / 128.0F);
  EXPECT_FLOAT_EQ(screen_hzb.screen_pos_to_hzb_uv_scale_x, 0.0F);
  EXPECT_FLOAT_EQ(screen_hzb.screen_pos_to_hzb_uv_scale_y, 0.0F);
  EXPECT_FLOAT_EQ(screen_hzb.screen_pos_to_hzb_uv_bias_x, 0.0F);
  EXPECT_FLOAT_EQ(screen_hzb.screen_pos_to_hzb_uv_bias_y, 0.0F);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage5BuildsFromValidIncompleteDepthProducts)
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

  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 0U });

  auto framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage5IncompleteDepth");
  const auto view_id = ViewId { 22U };
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto render_context = MakeSceneRenderContext(std::shared_ptr<Scene> {}, view_id,
    resolved_view, oxygen::observer_ptr { framebuffer.get() });
  render_context.frame_sequence = oxygen::frame::SequenceNumber { 4U };
  render_context.frame_slot = oxygen::frame::Slot { 0U };

  scene_renderer.OnFrameStart(frame_context);
  scene_renderer.OnRender(render_context);

  const auto& screen_hzb = scene_renderer.GetPublishedScreenHzbBindings();
  EXPECT_EQ(render_context.current_view.depth_prepass_completeness,
    oxygen::vortex::DepthPrePassCompleteness::kIncomplete);
  EXPECT_TRUE(render_context.current_view.scene_depth_product_valid);
  EXPECT_TRUE(render_context.current_view.CanBuildScreenHzb());
  EXPECT_TRUE(render_context.current_view.screen_hzb_available);
  EXPECT_TRUE(screen_hzb.furthest_srv.IsValid());
  EXPECT_TRUE((screen_hzb.flags
                 & oxygen::vortex::kScreenHzbFrameBindingsFlagAvailable)
    != 0U);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage5PreviousFrameHzbIsPublishedAfterTheSecondFrame)
{
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig {
      .extent = { 96U, 54U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);

  auto frame_context = FrameContext {};
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Stage5PreviousFrame", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });
  auto framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage5PreviousFrame");
  const auto view_id = ViewId { 23U };
  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto render_context = MakeSceneRenderContext(
    scene, view_id, resolved_view, oxygen::observer_ptr { framebuffer.get() });

  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 5U }, oxygen::frame::Slot { 0U });
  render_context.frame_sequence = oxygen::frame::SequenceNumber { 5U };
  render_context.frame_slot = oxygen::frame::Slot { 0U };
  scene_renderer.OnFrameStart(frame_context);
  scene_renderer.OnRender(render_context);

  const auto* first_current
    = render_context.current_view.screen_hzb_furthest_texture.get();
  ASSERT_NE(first_current, nullptr);
  EXPECT_FALSE(render_context.current_view.screen_hzb_has_previous);
  EXPECT_EQ(render_context.current_view.screen_hzb_previous_furthest_texture.get(),
    nullptr);

  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  render_context.frame_sequence = oxygen::frame::SequenceNumber { 6U };
  render_context.frame_slot = oxygen::frame::Slot { 1U };
  scene_renderer.OnFrameStart(frame_context);
  scene_renderer.OnRender(render_context);

  ASSERT_NE(render_context.current_view.screen_hzb_furthest_texture.get(), nullptr);
  EXPECT_TRUE(render_context.current_view.screen_hzb_available);
  EXPECT_TRUE(render_context.current_view.screen_hzb_has_previous);
  EXPECT_NE(render_context.current_view.screen_hzb_previous_furthest_texture.get(),
    nullptr);
  EXPECT_TRUE(render_context.current_view.screen_hzb_previous_furthest_srv.IsValid());
  EXPECT_NE(render_context.current_view.screen_hzb_furthest_texture.get(), first_current);
  EXPECT_EQ(render_context.current_view.screen_hzb_previous_furthest_texture.get(),
    first_current);
}

NOLINT_TEST_F(SceneRendererPublicationTest,
  Stage15RunsThroughEnvironmentLightingServiceAndKeepsAmbientBridgeOptIn)
{
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig {
      .extent = { 96U, 54U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);

  auto frame_context = FrameContext {};
  PrepareFrameContext(
    frame_context, oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto scene = std::make_shared<Scene>(
    "SceneRendererPublicationTest.Stage15Environment", 16U);
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });

  auto framebuffer
    = MakeFramebuffer("SceneRendererPublicationTest.Stage15Environment");
  const auto view_id = ViewId { 12U };
  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto render_context = MakeSceneRenderContext(
    scene, view_id, resolved_view, oxygen::observer_ptr { framebuffer.get() });
  auto composition_view = CompositionView {};
  composition_view.id = view_id;
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  render_context.frame_views.front().composition_view
    = oxygen::observer_ptr<const CompositionView> { &composition_view };
  render_context.current_view.composition_view
    = oxygen::observer_ptr<const CompositionView> { &composition_view };
  render_context.current_view.with_atmosphere = composition_view.with_atmosphere;
  render_context.current_view.with_height_fog = composition_view.with_height_fog;
  render_context.current_view.with_local_fog = composition_view.with_local_fog;

  scene_renderer.OnFrameStart(frame_context);
  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  scene_renderer.OnRender(render_context);

  EXPECT_NE(scene_renderer.GetPublishedViewFrameBindings().environment_frame_slot,
    oxygen::kInvalidShaderVisibleIndex);
  const auto& environment_state = scene_renderer.GetLastEnvironmentLightingState();
  EXPECT_TRUE(environment_state.owned_by_environment_service);
  EXPECT_TRUE(environment_state.atmosphere_requested);
  EXPECT_TRUE(environment_state.atmosphere_executed);
  EXPECT_EQ(environment_state.sky_draw_count, 1U);
  EXPECT_EQ(environment_state.atmosphere_draw_count, 1U);
  EXPECT_EQ(environment_state.fog_draw_count, 1U);
  EXPECT_EQ(environment_state.total_draw_count, 3U);
  EXPECT_TRUE(environment_state.published_bindings);
  EXPECT_EQ(environment_state.published_environment_frame_slot,
    scene_renderer.GetPublishedViewFrameBindings().environment_frame_slot);
  EXPECT_EQ(graphics_->draw_log_.draws.size(),
    environment_state.total_draw_count);
  EXPECT_FALSE(environment_state.ambient_bridge_published);
  EXPECT_EQ(environment_state.ambient_bridge_irradiance_srv,
    oxygen::kInvalidShaderVisibleIndex);

  const auto has_pipeline =
    [this](const std::string_view pipeline_name, const std::string_view source_path,
      const std::string_view entry_point, const bool expect_alpha_blend) -> bool {
    for (const auto& bind : graphics_->graphics_pipeline_log_.binds) {
      const auto& pixel_shader = bind.desc.PixelShader();
      if (pixel_shader.has_value() && bind.desc.GetName() == pipeline_name
        && pixel_shader->source_path == source_path
        && pixel_shader->entry_point == entry_point) {
        const auto& blend_state = bind.desc.BlendState();
        if (!expect_alpha_blend) {
          return blend_state.empty()
            || std::ranges::all_of(blend_state, [](const auto& target) {
                 return !target.blend_enable;
               });
        }
        return !blend_state.empty()
          && std::ranges::all_of(blend_state, [](const auto& target) {
               return target.blend_enable
                 && target.src_blend
                   == oxygen::graphics::BlendFactor::kSrcAlpha
                 && target.dest_blend
                   == oxygen::graphics::BlendFactor::kInvSrcAlpha;
             });
      }
    }
    return false;
  };

  EXPECT_TRUE(has_pipeline("Vortex.Environment.Sky",
    "Vortex/Services/Environment/Sky.hlsl", "VortexSkyPassPS", false));
  EXPECT_TRUE(has_pipeline("Vortex.Environment.Atmosphere",
    "Vortex/Services/Environment/AtmosphereCompose.hlsl",
    "VortexAtmosphereComposePS", true));
  EXPECT_TRUE(has_pipeline("Vortex.Environment.Fog",
    "Vortex/Services/Environment/Fog.hlsl", "VortexFogPassPS", true));
}

} // namespace
