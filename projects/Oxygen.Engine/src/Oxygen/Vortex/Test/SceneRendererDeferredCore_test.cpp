//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Types/PassMask.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::FrameContext;
using oxygen::graphics::QueueRole;
using oxygen::scene::DirectionalLight;
using oxygen::scene::PointLight;
using oxygen::scene::Scene;
using oxygen::scene::SpotLight;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneRenderer;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::ShadingMode;
using oxygen::vortex::ViewFrameBindings;
using oxygen::vortex::testing::FakeGraphics;

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  auto input = std::ifstream(path);
  EXPECT_TRUE(input.is_open()) << "failed to open " << path.generic_string();
  return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

auto SourceRoot() -> std::filesystem::path
{
  return std::filesystem::path { __FILE__ }.parent_path().parent_path()
    .parent_path();
}

auto ContainsAll(
  std::string_view haystack, std::initializer_list<std::string_view> needles)
  -> bool
{
  return std::ranges::all_of(needles, [&haystack](const auto needle) -> bool {
    return haystack.contains(needle);
  });
}

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics,
  const CapabilitySet capabilities = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kLightingData)
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

auto MakePerspectiveResolvedView(const float width, const float height)
  -> ResolvedView
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
  params.proj_matrix = glm::perspective(
    glm::radians(60.0F), width / height, 0.1F, 1000.0F);
  params.camera_position = glm::vec3(0.0F, 0.0F, 0.0F);
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
    view_constants_buffer_ = graphics_->CreateBuffer({
      .size_bytes = 1024U,
      .usage = oxygen::graphics::BufferUsage::kConstant,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = "SceneRendererDeferredCoreTest.ViewConstants",
    });

    scene_ = std::make_shared<Scene>("SceneRendererDeferredCoreTest", 16U);
    frame_context_.SetScene(oxygen::observer_ptr<Scene> { scene_.get() });
    static_cast<void>(
      frame_context_.RegisterView(MakeSceneView(first_view_id_, 64.0F, 64.0F)));
    static_cast<void>(
      frame_context_.RegisterView(MakeSceneView(second_view_id_, 96.0F, 54.0F)));

    first_resolved_view_ = MakeResolvedView(64.0F, 64.0F);
    second_resolved_view_ = MakeResolvedView(96.0F, 54.0F);
  }

  void UpdateSceneTransforms()
  {
    static_cast<void>(scene_->Traverse().UpdateTransforms());
  }

  auto RenderForView(
    const ViewId active_view_id, const ResolvedView& active_view) -> RenderContext
  {
    scene_renderer_->OnFrameStart(frame_context_);
    UpdateSceneTransforms();

    auto published_bindings = ViewFrameBindings {};
    published_bindings.scene_texture_frame_slot = oxygen::ShaderVisibleIndex { 9001U };
    scene_renderer_->PublishViewFrameBindings(
      active_view_id, published_bindings, oxygen::ShaderVisibleIndex { 9002U });

    auto context = RenderContext {};
    context.scene = oxygen::observer_ptr<Scene> { scene_.get() };
    context.frame_slot = oxygen::frame::Slot { 1U };
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
    context.view_constants = view_constants_buffer_;

    scene_renderer_->OnRender(context);
    return context;
  }

  auto AddDirectionalLight(std::string_view name) -> oxygen::scene::SceneNode
  {
    auto node = scene_->CreateNode(std::string(name));
    auto light = std::make_unique<DirectionalLight>();
    light->Common().color_rgb = { 1.0F, 0.95F, 0.8F };
    light->SetIntensityLux(1500.0F);
    EXPECT_TRUE(node.AttachLight(std::move(light)));
    UpdateSceneTransforms();
    return node;
  }

  auto AddPointLight(std::string_view name) -> oxygen::scene::SceneNode
  {
    auto node = scene_->CreateNode(std::string(name));
    auto light = std::make_unique<PointLight>();
    light->Common().color_rgb = { 0.4F, 0.7F, 1.0F };
    light->SetRange(6.0F);
    light->SetLuminousFluxLm(1200.0F);
    EXPECT_TRUE(node.AttachLight(std::move(light)));
    UpdateSceneTransforms();
    return node;
  }

  auto AddSpotLight(std::string_view name) -> oxygen::scene::SceneNode
  {
    auto node = scene_->CreateNode(std::string(name));
    auto light = std::make_unique<SpotLight>();
    light->Common().color_rgb = { 1.0F, 0.6F, 0.3F };
    light->SetRange(8.0F);
    light->SetLuminousFluxLm(900.0F);
    light->SetInnerConeAngleRadians(0.35F);
    light->SetOuterConeAngleRadians(0.65F);
    EXPECT_TRUE(node.AttachLight(std::move(light)));
    UpdateSceneTransforms();
    return node;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
  std::unique_ptr<SceneRenderer> scene_renderer_;
  std::shared_ptr<oxygen::graphics::Buffer> view_constants_buffer_;
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
  context.frame_slot = oxygen::frame::Slot { 1U };
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

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DepthPrepassRecordsRealDrawWorkFromPreparedMetadata)
{
  auto scene_config = SceneTexturesConfig {
    .extent = { 64U, 64U },
    .enable_velocity = true,
    .enable_custom_depth = false,
    .gbuffer_count = 4U,
    .msaa_sample_count = 1U,
  };
  auto depth_prepass = oxygen::vortex::DepthPrepassModule(*renderer_, scene_config);
  auto scene_textures = oxygen::vortex::SceneTextures(*graphics_, scene_config);

  auto draw_metadata = std::array<oxygen::vortex::DrawMetadata, 1> {};
  draw_metadata.front().is_indexed = 0U;
  draw_metadata.front().instance_count = 1U;
  draw_metadata.front().vertex_count = 3U;
  draw_metadata.front().flags
    = oxygen::vortex::PassMask { oxygen::vortex::PassMaskBit::kOpaque };

  auto prepared_frame = oxygen::vortex::PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes
    = std::as_bytes(std::span(draw_metadata));

  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.current_view.prepared_frame
    = oxygen::observer_ptr<const oxygen::vortex::PreparedSceneFrame> {
      &prepared_frame,
    };
  context.current_view.resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &first_resolved_view_ };
  context.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "SceneRendererDeferredCoreTest.DepthPrepassViewConstants",
  });

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();

  depth_prepass.SetConfig(oxygen::vortex::DepthPrepassConfig {
    .mode = oxygen::vortex::DepthPrePassMode::kOpaqueAndMasked,
    .write_velocity = true,
  });
  depth_prepass.Execute(context, scene_textures);

  ASSERT_EQ(graphics_->draw_log_.draws.size(), 1U);
  EXPECT_EQ(graphics_->draw_log_.draws.front().vertex_num, 3U);
  ASSERT_FALSE(graphics_->graphics_pipeline_log_.binds.empty());
  EXPECT_EQ(graphics_->graphics_pipeline_log_.binds.back().desc.GetName(),
    "Vortex.DepthPrepass.OpaqueVelocity");
  EXPECT_TRUE(depth_prepass.HasPublishedDepthProducts());
  EXPECT_EQ(depth_prepass.GetCompleteness(),
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
  context.frame_slot = oxygen::frame::Slot { 1U };
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
  context.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "SceneRendererDeferredCoreTest.ForwardModeViewConstants",
  });
  auto published_bindings = ViewFrameBindings {};
  published_bindings.scene_texture_frame_slot = oxygen::ShaderVisibleIndex { 9001U };
  scene_renderer_->PublishViewFrameBindings(
    first_view_id_, published_bindings, oxygen::ShaderVisibleIndex { 9002U });

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
  auto scene_config = SceneTexturesConfig {
    .extent = { 64U, 64U },
    .enable_velocity = true,
    .enable_custom_depth = false,
    .gbuffer_count = 4U,
    .msaa_sample_count = 1U,
  };
  auto base_pass = oxygen::vortex::BasePassModule(*renderer_, scene_config);
  auto scene_textures = oxygen::vortex::SceneTextures(*graphics_,
    scene_config);

  auto render_items = std::vector<oxygen::vortex::sceneprep::RenderItemData>(1U);
  render_items.front().main_view_visible = true;

  auto prepared_frame = oxygen::vortex::PreparedSceneFrame {};
  prepared_frame.render_items
    = std::span<const oxygen::vortex::sceneprep::RenderItemData>(
      render_items.data(), render_items.size());

  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.current_view.prepared_frame
    = oxygen::observer_ptr<const oxygen::vortex::PreparedSceneFrame> {
      &prepared_frame,
    };
  context.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "SceneRendererDeferredCoreTest.BasePassViewConstants",
  });

  base_pass.SetConfig(oxygen::vortex::BasePassConfig {
    .write_velocity = true,
    .early_z_pass_done = false,
    .shading_mode = ShadingMode::kDeferred,
  });

  base_pass.Execute(context, scene_textures);

  EXPECT_TRUE(base_pass.HasCompletedVelocityForDynamicGeometry());
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  BasePassRequiresPreparedFrameBeforePublishingProducts)
{
  auto scene_config = SceneTexturesConfig {
    .extent = { 64U, 64U },
    .enable_velocity = true,
    .enable_custom_depth = false,
    .gbuffer_count = 4U,
    .msaa_sample_count = 1U,
  };
  auto base_pass = oxygen::vortex::BasePassModule(*renderer_, scene_config);
  auto scene_textures = oxygen::vortex::SceneTextures(*graphics_, scene_config);
  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "SceneRendererDeferredCoreTest.BasePassGuardViewConstants",
  });

  base_pass.SetConfig(oxygen::vortex::BasePassConfig {
    .write_velocity = true,
    .early_z_pass_done = false,
    .shading_mode = ShadingMode::kDeferred,
  });

  base_pass.Execute(context, scene_textures);

  EXPECT_FALSE(base_pass.HasPublishedBasePassProducts());
  EXPECT_FALSE(base_pass.HasCompletedVelocityForDynamicGeometry());
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest, GBufferDebugViewsAreAvailable)
{
  const auto context = RenderForView(first_view_id_, first_resolved_view_);
  const auto& bindings = scene_renderer_->GetSceneTextureBindings();

  EXPECT_EQ(context.current_view.depth_prepass_completeness,
    oxygen::vortex::DepthPrePassCompleteness::kComplete);
  for (const auto gbuffer_srv : bindings.gbuffer_srvs) {
    EXPECT_NE(gbuffer_srv, oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  }

  const auto source_root = SourceRoot();
  const auto shader_path = source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl";
  const auto catalog_path
    = source_root / "Graphics/Direct3D12/Shaders/EngineShaderCatalog.h";

  EXPECT_TRUE(std::filesystem::exists(shader_path))
    << shader_path.generic_string();
  EXPECT_TRUE(std::filesystem::exists(catalog_path))
    << catalog_path.generic_string();

  const auto shader_source = ReadTextFile(shader_path);
  EXPECT_TRUE(ContainsAll(shader_source,
    { "DEBUG_BASE_COLOR",
      "DEBUG_WORLD_NORMALS",
      "DEBUG_ROUGHNESS",
      "DEBUG_METALNESS" }));

  const auto catalog_source = ReadTextFile(catalog_path);
  EXPECT_TRUE(ContainsAll(catalog_source,
    { "Vortex/Stages/BasePass/BasePassDebugView.hlsl",
      "DEBUG_BASE_COLOR",
      "DEBUG_WORLD_NORMALS",
      "DEBUG_ROUGHNESS",
      "DEBUG_METALNESS" }));
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DeferredLightingConsumesPublishedGBuffers)
{
  static_cast<void>(AddDirectionalLight("KeyLight"));

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  static_cast<void>(RenderForView(first_view_id_, first_resolved_view_));

  const auto& state = scene_renderer_->GetLastDeferredLightingState();
  const auto& bindings = scene_renderer_->GetSceneTextureBindings();

  EXPECT_TRUE(state.consumed_published_scene_textures);
  EXPECT_EQ(state.published_view_id, first_view_id_);
  EXPECT_EQ(state.published_view_frame_bindings_slot,
    scene_renderer_->GetPublishedViewFrameBindingsSlot());
  EXPECT_EQ(state.published_scene_texture_frame_slot,
    scene_renderer_->GetPublishedViewFrameBindings().scene_texture_frame_slot);
  EXPECT_EQ(state.consumed_scene_depth_srv, bindings.scene_depth_srv);
  EXPECT_EQ(state.consumed_gbuffer_srvs, bindings.gbuffer_srvs);
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 1U);
  EXPECT_TRUE(std::ranges::any_of(graphics_->graphics_pipeline_log_.binds,
    [](const auto& bind) -> bool {
      return bind.desc.GetName() == "Vortex.DeferredLight.Directional";
    }));
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DeferredLightingAccumulatesIntoSceneColor)
{
  static_cast<void>(AddDirectionalLight("Sun"));

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  static_cast<void>(RenderForView(first_view_id_, first_resolved_view_));

  const auto& state = scene_renderer_->GetLastDeferredLightingState();
  const auto& bindings = scene_renderer_->GetSceneTextureBindings();

  EXPECT_TRUE(state.accumulated_into_scene_color);
  EXPECT_EQ(state.consumed_scene_color_uav, bindings.scene_color_uav);
  EXPECT_EQ(state.directional_light_count, 1U);
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 1U);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Directional";
              }),
    1);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DeferredLightingUsesOutsideVolumeLocalLights)
{
  const auto outside_view = MakePerspectiveResolvedView(64.0F, 64.0F);
  auto point = AddPointLight("PointFill");
  auto spot = AddSpotLight("SpotRim");
  point.GetTransform().SetLocalPosition({ 0.0F, 20.0F, 0.0F });
  spot.GetTransform().SetLocalPosition({ 0.0F, 20.0F, 0.0F });
  UpdateSceneTransforms();

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  static_cast<void>(RenderForView(first_view_id_, outside_view));

  const auto& state = scene_renderer_->GetLastDeferredLightingState();

  EXPECT_TRUE(state.used_outside_volume_local_lights);
  EXPECT_FALSE(state.used_camera_inside_local_lights);
  EXPECT_FALSE(state.used_direct_local_light_fallbacks);
  EXPECT_EQ(state.point_light_count, 1U);
  EXPECT_EQ(state.spot_light_count, 1U);
  EXPECT_EQ(state.local_light_count, 2U);
  EXPECT_EQ(state.outside_volume_local_light_count, state.local_light_count);
  EXPECT_EQ(state.camera_inside_local_light_count, 0U);
  EXPECT_EQ(state.direct_local_light_pass_count, state.local_light_count);
  EXPECT_EQ(state.direct_local_light_fallback_count, 0U);
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Point.Lighting";
              }),
    1);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Spot.Lighting";
              }),
    1);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DeferredLightingUsesInsideVolumePathWhenCameraStartsInsideLocalLights)
{
  const auto inside_view = MakePerspectiveResolvedView(64.0F, 64.0F);
  static_cast<void>(AddPointLight("PointFill"));
  static_cast<void>(AddSpotLight("SpotRim"));

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  static_cast<void>(RenderForView(first_view_id_, inside_view));

  const auto& state = scene_renderer_->GetLastDeferredLightingState();

  EXPECT_FALSE(state.used_outside_volume_local_lights);
  EXPECT_TRUE(state.used_camera_inside_local_lights);
  EXPECT_FALSE(state.used_direct_local_light_fallbacks);
  EXPECT_EQ(state.point_light_count, 1U);
  EXPECT_EQ(state.spot_light_count, 1U);
  EXPECT_EQ(state.local_light_count, 2U);
  EXPECT_EQ(state.outside_volume_local_light_count, 0U);
  EXPECT_EQ(state.camera_inside_local_light_count, state.local_light_count);
  EXPECT_EQ(state.direct_local_light_pass_count, state.local_light_count);
  EXPECT_EQ(state.direct_local_light_fallback_count, 0U);
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Point.InsideVolumeLighting";
              }),
    1);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Spot.InsideVolumeLighting";
              }),
    1);
}

NOLINT_TEST_F(SceneRendererDeferredCoreTest,
  DeferredLightingUsesDirectFallbackForNonPerspectiveViews)
{
  static_cast<void>(AddPointLight("PointFill"));
  static_cast<void>(AddSpotLight("SpotRim"));

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  static_cast<void>(RenderForView(first_view_id_, first_resolved_view_));

  const auto& state = scene_renderer_->GetLastDeferredLightingState();

  EXPECT_FALSE(state.used_outside_volume_local_lights);
  EXPECT_FALSE(state.used_camera_inside_local_lights);
  EXPECT_TRUE(state.used_direct_local_light_fallbacks);
  EXPECT_EQ(state.point_light_count, 1U);
  EXPECT_EQ(state.spot_light_count, 1U);
  EXPECT_EQ(state.local_light_count, 2U);
  EXPECT_EQ(state.outside_volume_local_light_count, 0U);
  EXPECT_EQ(state.camera_inside_local_light_count, 0U);
  EXPECT_EQ(state.direct_local_light_pass_count, state.local_light_count);
  EXPECT_EQ(state.direct_local_light_fallback_count, state.local_light_count);
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Point.DirectFallbackLighting";
              }),
    1);
  EXPECT_EQ(std::ranges::count_if(graphics_->graphics_pipeline_log_.binds,
              [](const auto& bind) -> bool {
                return bind.desc.GetName()
                  == "Vortex.DeferredLight.Spot.DirectFallbackLighting";
              }),
    1);
}

NOLINT_TEST(SceneRendererDeferredCoreMeshProcessorTest,
  BasePassMeshProcessorHonorsVelocityPolicy)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);
  auto mesh_processor
    = oxygen::vortex::BasePassMeshProcessor(*renderer);

  auto render_items = std::vector<oxygen::vortex::sceneprep::RenderItemData>(1U);
  render_items.front().main_view_visible = true;

  auto prepared_frame = oxygen::vortex::PreparedSceneFrame {};
  prepared_frame.render_items
    = std::span<const oxygen::vortex::sceneprep::RenderItemData>(
      render_items.data(), render_items.size());

  mesh_processor.BuildDrawCommands(
    prepared_frame, ShadingMode::kDeferred, false);
  auto draw_commands = mesh_processor.GetDrawCommands();
  ASSERT_EQ(draw_commands.size(), 1U);
  EXPECT_FALSE(draw_commands.front().writes_velocity);

  mesh_processor.BuildDrawCommands(
    prepared_frame, ShadingMode::kDeferred, true);
  draw_commands = mesh_processor.GetDrawCommands();
  ASSERT_EQ(draw_commands.size(), 1U);
  EXPECT_TRUE(draw_commands.front().writes_velocity);
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
  context.frame_slot = oxygen::frame::Slot { 1U };
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
  const auto& bindings = scene_renderer->GetSceneTextureBindings();
  EXPECT_EQ(bindings.scene_color_srv,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.scene_color_uav,
    oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  for (const auto gbuffer_srv : bindings.gbuffer_srvs) {
    EXPECT_EQ(gbuffer_srv, oxygen::vortex::SceneTextureBindings::kInvalidIndex);
  }
}

} // namespace
