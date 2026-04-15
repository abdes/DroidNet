//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <glm/mat4x4.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::FrameContext;
using oxygen::graphics::QueueRole;
using oxygen::scene::Scene;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneRenderer;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::ShadingMode;
using oxygen::vortex::testing::FakeGraphics;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics,
  const CapabilitySet capabilities = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading)
  -> std::shared_ptr<Renderer>
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics->QueueKeyFor(QueueRole::kGraphics).get();
  return { new Renderer(
             std::weak_ptr<Graphics>(graphics), std::move(config), capabilities),
    DestroyRenderer };
}

auto MakeSceneView(const ViewId view_id, const float width, const float height)
  -> oxygen::engine::ViewContext
{
  auto view = oxygen::engine::ViewContext {};
  view.id = view_id;
  view.view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  view.metadata.name = "SceneView";
  view.metadata.purpose = "scene";
  view.metadata.is_scene_view = true;
  return view;
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

class SceneRendererDeferredCoreTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);

    auto scene_config = SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = true,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    };
    scene_renderer_ = std::make_unique<SceneRenderer>(
      *renderer_, *graphics_, scene_config, ShadingMode::kDeferred);

    scene_ = std::make_shared<Scene>("SceneRendererDeferredCoreTest", 16U);
    frame_context_.SetScene(oxygen::observer_ptr<Scene> { scene_.get() });
    static_cast<void>(
      frame_context_.RegisterView(MakeSceneView(first_view_id_, 64.0F, 64.0F)));
    static_cast<void>(
      frame_context_.RegisterView(MakeSceneView(second_view_id_, 96.0F, 54.0F)));

    first_resolved_view_ = MakeResolvedView(64.0F, 64.0F);
    second_resolved_view_ = MakeResolvedView(96.0F, 54.0F);
  }

  auto RenderForView(
    const ViewId active_view_id, const ResolvedView& active_view) -> RenderContext
  {
    scene_renderer_->OnFrameStart(frame_context_);

    auto context = RenderContext {};
    context.scene = oxygen::observer_ptr<Scene> { scene_.get() };
    context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
    context.active_view_index
      = active_view_id == first_view_id_ ? std::size_t { 0U } : std::size_t { 1U };
    context.frame_views.push_back({
      .view_id = first_view_id_,
      .is_scene_view = true,
      .composition_view = {},
      .shading_mode_override = {},
      .resolved_view
      = oxygen::observer_ptr<const ResolvedView> { &first_resolved_view_ },
      .primary_target = {},
    });
    context.frame_views.push_back({
      .view_id = second_view_id_,
      .is_scene_view = true,
      .composition_view = {},
      .shading_mode_override = {},
      .resolved_view
      = oxygen::observer_ptr<const ResolvedView> { &second_resolved_view_ },
      .primary_target = {},
    });
    context.current_view.view_id = active_view_id;
    context.current_view.exposure_view_id = active_view_id;
    context.current_view.resolved_view
      = oxygen::observer_ptr<const ResolvedView> { &active_view };

    scene_renderer_->OnRender(context);
    return context;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
  std::unique_ptr<SceneRenderer> scene_renderer_;
  std::shared_ptr<Scene> scene_;
  FrameContext frame_context_;
  ResolvedView first_resolved_view_ = MakeResolvedView(64.0F, 64.0F);
  ResolvedView second_resolved_view_ = MakeResolvedView(96.0F, 54.0F);
  const ViewId first_view_id_ { 101U };
  const ViewId second_view_id_ { 202U };
};

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  InitViewsPublishesPreparedSceneFrameForEverySceneView)
{
  const auto first_context = RenderForView(first_view_id_, first_resolved_view_);
  ASSERT_NE(first_context.current_view.prepared_frame.get(), nullptr);

  const auto second_context
    = RenderForView(second_view_id_, second_resolved_view_);
  ASSERT_NE(second_context.current_view.prepared_frame.get(), nullptr);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  InitViewsKeepsTheActiveViewPreparedFrameBoundToCurrentView)
{
  const auto first_context = RenderForView(first_view_id_, first_resolved_view_);
  const auto* first_prepared = first_context.current_view.prepared_frame.get();
  ASSERT_NE(first_prepared, nullptr);

  const auto second_context
    = RenderForView(second_view_id_, second_resolved_view_);
  const auto* second_prepared = second_context.current_view.prepared_frame.get();
  ASSERT_NE(second_prepared, nullptr);
  EXPECT_NE(second_prepared, first_prepared);

  const auto rebound_context = RenderForView(first_view_id_, first_resolved_view_);
  EXPECT_EQ(rebound_context.current_view.prepared_frame.get(), first_prepared);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DepthPrepassDisabledModeLeavesCompletenessDisabled)
{
  scene_renderer_->OnFrameStart(frame_context_);

  auto context = RenderContext {};
  context.scene = oxygen::observer_ptr<Scene> { scene_.get() };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.active_view_index = std::size_t { 0U };
  context.frame_views.push_back({
    .view_id = first_view_id_,
    .is_scene_view = true,
    .composition_view = {},
    .shading_mode_override = {},
    .resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &first_resolved_view_ },
    .primary_target = {},
  });
  context.current_view.view_id = first_view_id_;
  context.current_view.exposure_view_id = first_view_id_;
  context.current_view.resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &first_resolved_view_ };
  context.current_view.depth_prepass_mode = oxygen::vortex::DepthPrePassMode::kDisabled;

  scene_renderer_->OnRender(context);

  EXPECT_EQ(context.current_view.depth_prepass_completeness,
    oxygen::vortex::DepthPrePassCompleteness::kDisabled);
  EXPECT_EQ(scene_renderer_->GetSceneTextureBindings().scene_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(scene_renderer_->GetSceneTextureBindings().partial_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DepthPrepassPublishesSceneDepthAndPartialDepth)
{
  const auto context = RenderForView(first_view_id_, first_resolved_view_);

  EXPECT_EQ(context.current_view.depth_prepass_completeness,
    oxygen::vortex::DepthPrePassCompleteness::kComplete);
  EXPECT_NE(scene_renderer_->GetSceneTextureBindings().scene_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(scene_renderer_->GetSceneTextureBindings().partial_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(scene_renderer_->GetSceneTextureBindings().velocity_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DepthPrepassCompletenessControlsEarlyDepthContract)
{
  const auto context = RenderForView(first_view_id_, first_resolved_view_);

  EXPECT_TRUE(context.current_view.HasPlannedDepthPrePass());
  EXPECT_TRUE(context.current_view.IsEarlyDepthComplete());
  EXPECT_EQ(context.current_view.depth_prepass_completeness,
    oxygen::vortex::DepthPrePassCompleteness::kComplete);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest, BasePassPromotesGBuffersAtStage10)
{
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = true,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);

  scene_renderer.ApplyStage3DepthPrepassState();
  scene_renderer.ApplyStage9BasePassState();

  auto bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_EQ(bindings.scene_color_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_uav,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  for (const auto gbuffer_srv : bindings.gbuffer_srvs) {
    EXPECT_EQ(gbuffer_srv, oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  }
  EXPECT_NE(bindings.scene_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.velocity_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);

  scene_renderer.ApplyStage10RebuildState();

  bindings = scene_renderer.GetSceneTextureBindings();
  EXPECT_NE(bindings.scene_color_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.scene_color_uav,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  for (const auto gbuffer_srv : bindings.gbuffer_srvs) {
    EXPECT_NE(gbuffer_srv, oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  }
  EXPECT_NE(bindings.stencil_srv, oxygen::vortex::SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  BasePassRejectsForwardModeDuringPhase3)
{
  scene_renderer_->OnFrameStart(frame_context_);

  auto context = RenderContext {};
  context.scene = oxygen::observer_ptr<Scene> { scene_.get() };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.active_view_index = std::size_t { 0U };
  context.frame_views.push_back({
    .view_id = first_view_id_,
    .is_scene_view = true,
    .composition_view = {},
    .shading_mode_override = {},
    .resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &first_resolved_view_ },
    .primary_target = {},
  });
  context.current_view.view_id = first_view_id_;
  context.current_view.exposure_view_id = first_view_id_;
  context.current_view.resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &first_resolved_view_ };
  context.current_view.shading_mode_override = ShadingMode::kForward;

  scene_renderer_->OnRender(context);

  const auto& bindings = scene_renderer_->GetSceneTextureBindings();
  EXPECT_EQ(scene_renderer_->GetEffectiveShadingMode(context), ShadingMode::kForward);
  EXPECT_EQ(bindings.scene_color_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_uav,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  for (const auto gbuffer_srv : bindings.gbuffer_srvs) {
    EXPECT_EQ(gbuffer_srv, oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  }
  EXPECT_NE(bindings.scene_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_NE(bindings.partial_depth_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  BasePassCompletesVelocityForDynamicGeometry)
{
  auto base_pass = oxygen::vortex::BasePassModule(*renderer_);
  auto scene_textures = oxygen::vortex::SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });

  auto render_items = std::vector<oxygen::vortex::sceneprep::RenderItemData>(1U);
  render_items.front().main_view_visible = true;

  auto prepared_frame = oxygen::vortex::PreparedSceneFrame {};
  prepared_frame.render_items
    = std::span<const oxygen::vortex::sceneprep::RenderItemData>(
      render_items.data(), render_items.size());

  auto context = RenderContext {};
  context.current_view.prepared_frame
    = oxygen::observer_ptr<const oxygen::vortex::PreparedSceneFrame> {
      &prepared_frame,
    };

  base_pass.SetConfig(oxygen::vortex::BasePassConfig {
    .write_velocity = true,
    .early_z_pass_done = false,
    .shading_mode = ShadingMode::kDeferred,
  });

  base_pass.Execute(context, scene_textures);

  EXPECT_TRUE(base_pass.HasCompletedVelocityForDynamicGeometry());
}

NOLINT_TEST(SceneRendererDeferredCoreCapabilityTest,
  DepthPrepassStaysDisabledWithoutDeferredShadingCapability)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(
    graphics, RendererCapabilityFamily::kScenePreparation);
  auto scene_renderer = std::make_unique<SceneRenderer>(*renderer, *graphics,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = true,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    },
    ShadingMode::kDeferred);
  auto scene = std::make_shared<Scene>(
    "SceneRendererDeferredCoreCapabilityTest", 16U);
  auto frame_context = FrameContext {};
  frame_context.SetScene(oxygen::observer_ptr<Scene> { scene.get() });
  static_cast<void>(
    frame_context.RegisterView(MakeSceneView(ViewId { 11U }, 64.0F, 64.0F)));

  scene_renderer->OnFrameStart(frame_context);

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto context = RenderContext {};
  context.scene = oxygen::observer_ptr<Scene> { scene.get() };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.active_view_index = std::size_t { 0U };
  context.frame_views.push_back({
    .view_id = ViewId { 11U },
    .is_scene_view = true,
    .composition_view = {},
    .shading_mode_override = {},
    .resolved_view = oxygen::observer_ptr<const ResolvedView> { &resolved_view },
    .primary_target = {},
  });
  context.current_view.view_id = ViewId { 11U };
  context.current_view.exposure_view_id = ViewId { 11U };
  context.current_view.resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &resolved_view };

  scene_renderer->OnRender(context);

  EXPECT_EQ(context.current_view.depth_prepass_completeness,
    oxygen::vortex::DepthPrePassCompleteness::kDisabled);
}

} // namespace
