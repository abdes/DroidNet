//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Internal/CompositionPlanner.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ShaderDebugMode.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ShaderPassConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ToneMapPassConfig.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::observer_ptr;
using oxygen::ViewId;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::CompositionView;
using oxygen::vortex::DepthPrePassMode;
using oxygen::vortex::RenderMode;
using oxygen::vortex::ShaderDebugMode;
using oxygen::vortex::internal::CompositionPlanner;
using oxygen::vortex::internal::CompositionViewImpl;
using oxygen::vortex::internal::FramePlanBuilder;
using oxygen::vortex::internal::ToneMapPolicy;
using oxygen::vortex::internal::access::ViewLifecycleTagFactory;
using oxygen::vortex::testing::FakeGraphics;

auto MakeView() -> oxygen::View
{
  oxygen::View view {};
  view.viewport = {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 320.0F,
    .height = 180.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  return view;
}

auto MakeCompositeTarget(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Framebuffer>
{
  TextureDesc desc {};
  desc.width = 320;
  desc.height = 180;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = oxygen::TextureType::kTexture2D;
  desc.is_render_target = true;
  desc.is_shader_resource = true;
  desc.use_clear_value = true;
  desc.clear_value = { 0.0F, 0.0F, 0.0F, 1.0F };
  desc.initial_state = oxygen::graphics::ResourceStates::kCommon;
  desc.debug_name = "CompositionPlannerTestTarget";

  auto texture = graphics->CreateTexture(desc);
  FramebufferDesc fb_desc {};
  fb_desc.AddColorAttachment({ .texture = texture });
  return graphics->CreateFramebuffer(fb_desc);
}

auto MakeInputs(const ViewId published_view_id) -> FramePlanBuilder::Inputs
{
  static const oxygen::vortex::ToneMapPassConfig tone_map_config {};
  static const oxygen::vortex::ShaderPassConfig shader_pass_config {};
  return FramePlanBuilder::Inputs {
    .frame_settings = {},
    .pending_auto_exposure_reset = std::nullopt,
    .tone_map_pass_config = observer_ptr { &tone_map_config },
    .shader_pass_config = observer_ptr { &shader_pass_config },
    .resolve_published_view_id
    = [published_view_id](const ViewId) { return published_view_id; },
  };
}

auto MakeMappedInputs() -> FramePlanBuilder::Inputs
{
  static const oxygen::vortex::ToneMapPassConfig tone_map_config {};
  static const oxygen::vortex::ShaderPassConfig shader_pass_config {};
  return FramePlanBuilder::Inputs {
    .frame_settings = {},
    .pending_auto_exposure_reset = std::nullopt,
    .tone_map_pass_config = observer_ptr { &tone_map_config },
    .shader_pass_config = observer_ptr { &shader_pass_config },
    .resolve_published_view_id = [](const ViewId id) {
      return ViewId { id.get() + 1000U };
    },
  };
}

void PrepareView(CompositionViewImpl& view_impl, const CompositionView& desc,
  FakeGraphics& graphics)
{
  const auto tag = ViewLifecycleTagFactory::Get();
  view_impl.PrepareForRender(desc, 0U, graphics, tag);
}

TEST(CompositionPlannerTest, PrimarySceneViewUsesCopyTaskAtFullOpacity)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 101U }, MakeView(), oxygen::scene::SceneNode {});
  scene_view.opacity = 1.0F;

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 201U }));

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 1U);
  EXPECT_EQ(
    submission.tasks[0].type, oxygen::vortex::CompositingTaskType::kCopy);
  EXPECT_EQ(submission.tasks[0].copy.source_view_id, ViewId { 201U });
}

TEST(CompositionPlannerTest, PartialOpacitySceneViewUsesTextureBlendTask)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 102U }, MakeView(), oxygen::scene::SceneNode {});
  scene_view.opacity = 0.5F;

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 202U }));

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 1U);
  EXPECT_EQ(submission.tasks[0].type,
    oxygen::vortex::CompositingTaskType::kBlendTexture);
  EXPECT_FLOAT_EQ(submission.tasks[0].texture_blend.alpha, 0.5F);
}

TEST(CompositionPlannerTest, OverlayViewUsesTextureBlendTask)
{
  auto graphics = std::make_shared<FakeGraphics>();

  const auto overlay = [](oxygen::graphics::CommandRecorder&) { };
  CompositionView overlay_view = CompositionView::ForHud(
    ViewId { 103U }, CompositionView::kZOrderGameUI, MakeView(), overlay);
  overlay_view.opacity = 1.0F;

  CompositionViewImpl view_impl;
  PrepareView(view_impl, overlay_view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 203U }));

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 1U);
  EXPECT_EQ(submission.tasks[0].type,
    oxygen::vortex::CompositingTaskType::kBlendTexture);
  EXPECT_FLOAT_EQ(submission.tasks[0].texture_blend.alpha, 1.0F);
}

TEST(CompositionPlannerTest, SceneHudAndImGuiLayersStayOrderedOnSingleViewPath)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 108U }, MakeView(), oxygen::scene::SceneNode {});
  scene_view.opacity = 1.0F;

  const auto overlay = [](oxygen::graphics::CommandRecorder&) { };
  CompositionView hud_view = CompositionView::ForHud(
    ViewId { 109U }, CompositionView::kZOrderGameUI, MakeView(), overlay);
  hud_view.opacity = 0.75F;

  CompositionView imgui_view
    = CompositionView::ForImGui(ViewId { 110U }, MakeView(), overlay);
  imgui_view.opacity = 1.0F;

  CompositionViewImpl scene_impl;
  CompositionViewImpl hud_impl;
  CompositionViewImpl imgui_impl;
  PrepareView(scene_impl, scene_view, *graphics);
  PrepareView(hud_impl, hud_view, *graphics);
  PrepareView(imgui_impl, imgui_view, *graphics);

  FramePlanBuilder builder;
  std::array views { &scene_impl, &hud_impl, &imgui_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 208U }));

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 3U);
  EXPECT_EQ(submission.tasks[0].type,
    oxygen::vortex::CompositingTaskType::kCopy);
  EXPECT_EQ(submission.tasks[0].copy.source_view_id, ViewId { 208U });

  EXPECT_EQ(submission.tasks[1].type,
    oxygen::vortex::CompositingTaskType::kBlendTexture);
  EXPECT_FLOAT_EQ(submission.tasks[1].texture_blend.alpha, 0.75F);

  EXPECT_EQ(submission.tasks[2].type,
    oxygen::vortex::CompositingTaskType::kBlendTexture);
  EXPECT_FLOAT_EQ(submission.tasks[2].texture_blend.alpha, 1.0F);
}

TEST(CompositionPlannerTest, SceneViewPlansDepthPrePassByDefault)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 104U }, MakeView(), oxygen::scene::SceneNode {});

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 204U }));

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  const auto& plan = builder.GetFrameViewPackets().front().Plan();
  EXPECT_TRUE(plan.HasSceneLinearPath());
  EXPECT_EQ(plan.GetDepthPrePassMode(), DepthPrePassMode::kOpaqueAndMasked);
  EXPECT_TRUE(plan.WantsDepthPrePass());
}

TEST(CompositionPlannerTest, DisabledDepthPrePassPolicyPropagatesToScenePlan)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 105U }, MakeView(), oxygen::scene::SceneNode {});

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics);

  auto inputs = MakeInputs(ViewId { 205U });
  inputs.frame_settings.depth_prepass_mode = DepthPrePassMode::kDisabled;

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    inputs);

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  const auto& plan = builder.GetFrameViewPackets().front().Plan();
  EXPECT_EQ(plan.GetDepthPrePassMode(), DepthPrePassMode::kDisabled);
  EXPECT_FALSE(plan.WantsDepthPrePass());
}

TEST(CompositionPlannerTest, WireframeScenePlanDisablesDepthPrePass)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 106U }, MakeView(), oxygen::scene::SceneNode {});
  scene_view.force_wireframe = true;

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 206U }));

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  const auto& plan = builder.GetFrameViewPackets().front().Plan();
  EXPECT_EQ(plan.EffectiveRenderMode(), RenderMode::kWireframe);
  EXPECT_EQ(plan.GetDepthPrePassMode(), DepthPrePassMode::kDisabled);
  EXPECT_FALSE(plan.WantsDepthPrePass());
}

TEST(CompositionPlannerTest, DepthPrepassDebugModesForceNeutralToneMapping)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 107U }, MakeView(), oxygen::scene::SceneNode {});

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics);

  auto inputs = MakeInputs(ViewId { 207U });
  auto shader_pass_config = oxygen::vortex::ShaderPassConfig {};
  shader_pass_config.debug_mode = ShaderDebugMode::kSceneDepthRaw;
  inputs.shader_pass_config = observer_ptr { &shader_pass_config };

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    inputs);

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  const auto& plan = builder.GetFrameViewPackets().front().Plan();
  EXPECT_EQ(plan.GetToneMapPolicy(), ToneMapPolicy::kNeutral);
}

TEST(CompositionPlannerTest, PerViewRenderSettingsDoNotLeakBetweenPackets)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView first = CompositionView::ForScene(
    ViewId { 120U }, MakeView(), oxygen::scene::SceneNode {});
  first.render_settings.render_mode = RenderMode::kWireframe;
  first.render_settings.shader_debug_mode = ShaderDebugMode::kSceneDepthRaw;

  CompositionView second = CompositionView::ForScene(
    ViewId { 121U }, MakeView(), oxygen::scene::SceneNode {});
  second.render_settings.render_mode = RenderMode::kSolid;
  second.render_settings.shader_debug_mode = ShaderDebugMode::kDisabled;

  CompositionViewImpl first_impl;
  CompositionViewImpl second_impl;
  PrepareView(first_impl, first, *graphics);
  PrepareView(second_impl, second, *graphics);

  FramePlanBuilder builder;
  std::array views { &first_impl, &second_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeMappedInputs());

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 2U);
  const auto& first_plan = builder.GetFrameViewPackets()[0].Plan();
  const auto& second_plan = builder.GetFrameViewPackets()[1].Plan();
  EXPECT_EQ(first_plan.EffectiveRenderMode(), RenderMode::kWireframe);
  EXPECT_EQ(
    first_plan.EffectiveShaderDebugMode(), ShaderDebugMode::kSceneDepthRaw);
  EXPECT_EQ(first_plan.GetDepthPrePassMode(), DepthPrePassMode::kDisabled);
  EXPECT_EQ(second_plan.EffectiveRenderMode(), RenderMode::kSolid);
  EXPECT_EQ(
    second_plan.EffectiveShaderDebugMode(), ShaderDebugMode::kDisabled);
  EXPECT_EQ(
    second_plan.GetDepthPrePassMode(), DepthPrePassMode::kOpaqueAndMasked);
}

TEST(CompositionPlannerTest, PerViewStateHandleIsCopiedIntoFramePacket)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView view = CompositionView::ForScene(
    ViewId { 122U }, MakeView(), oxygen::scene::SceneNode {});
  view.view_state_handle = CompositionView::ViewStateHandle { 77U };

  CompositionViewImpl view_impl;
  PrepareView(view_impl, view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 222U }));

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  EXPECT_EQ(builder.GetFrameViewPackets().front().ViewStateHandle(),
    CompositionView::ViewStateHandle { 77U });
}

TEST(CompositionPlannerTest, ViewClassificationPayloadsCopyIntoFramePacket)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView view = CompositionView::ForScene(
    ViewId { 123U }, MakeView(), oxygen::scene::SceneNode {});
  view.view_kind = CompositionView::ViewKind::kAuxiliary;
  view.feature_mask.bits = CompositionView::ViewFeatureBits {
    CompositionView::ViewFeatureMask::kSceneLighting
    | CompositionView::ViewFeatureMask::kDiagnostics
  };
  view.surface_routes.push_back(CompositionView::ViewSurfaceRoute {
    .surface_id = CompositionView::SurfaceRouteId { 9U },
    .destination = MakeView().viewport,
    .blend_mode = CompositionView::SurfaceRouteBlendMode::kCopy,
  });
  view.overlay_policy.lanes = { CompositionView::OverlayLane::kWorldDepthAware,
    CompositionView::OverlayLane::kSurfaceScreen };
  view.produced_aux_outputs.push_back(CompositionView::AuxOutputDesc {
    .id = CompositionView::AuxOutputId { 5U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .debug_name = "Aux.Color",
  });
  view.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 6U },
    .required = false,
  });

  CompositionViewImpl view_impl;
  PrepareView(view_impl, view, *graphics);

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs(ViewId { 223U }));

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  const auto& packet = builder.GetFrameViewPackets().front();
  EXPECT_EQ(packet.Kind(), CompositionView::ViewKind::kAuxiliary);
  EXPECT_TRUE(packet.FeatureMask().Has(
    CompositionView::ViewFeatureMask::kSceneLighting));
  EXPECT_FALSE(
    packet.FeatureMask().Has(CompositionView::ViewFeatureMask::kShadows));
  ASSERT_EQ(packet.SurfaceRoutes().size(), 1U);
  EXPECT_EQ(packet.SurfaceRoutes()[0].surface_id,
    CompositionView::SurfaceRouteId { 9U });
  ASSERT_EQ(packet.GetOverlayPolicy().lanes.size(), 2U);
  EXPECT_EQ(packet.GetOverlayPolicy().lanes[0],
    CompositionView::OverlayLane::kWorldDepthAware);
  ASSERT_EQ(packet.ProducedAuxOutputs().size(), 1U);
  EXPECT_EQ(packet.ProducedAuxOutputs()[0].id,
    CompositionView::AuxOutputId { 5U });
  ASSERT_EQ(packet.ConsumedAuxOutputs().size(), 1U);
  EXPECT_FALSE(packet.ConsumedAuxOutputs()[0].required);
}

} // namespace
