//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Pipeline/DepthPrePassPolicy.h>
#include <Oxygen/Renderer/Pipeline/Internal/CompositionPlanner.h>
#include <Oxygen/Renderer/Pipeline/Internal/CompositionViewImpl.h>
#include <Oxygen/Renderer/Pipeline/Internal/FramePlanBuilder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>

namespace {

using oxygen::observer_ptr;
using oxygen::ViewId;
using oxygen::engine::ShaderDebugMode;
using oxygen::frame::SequenceNumber;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::TextureDesc;
using oxygen::renderer::CompositionView;
using oxygen::renderer::DepthPrePassMode;
using oxygen::renderer::RenderMode;
using oxygen::renderer::internal::CompositionPlanner;
using oxygen::renderer::internal::CompositionViewImpl;
using oxygen::renderer::internal::FramePlanBuilder;
using oxygen::renderer::internal::ToneMapPolicy;
using oxygen::renderer::internal::access::ViewLifecycleTagFactory;
using oxygen::renderer::testing::FakeGraphics;

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

auto MakeInputs() -> FramePlanBuilder::Inputs
{
  static const oxygen::engine::ToneMapPassConfig tone_map_config {};
  static const oxygen::engine::ShaderPassConfig shader_pass_config {};
  return FramePlanBuilder::Inputs {
    .frame_settings = {},
    .pending_auto_exposure_reset = std::nullopt,
    .tone_map_pass_config = observer_ptr { &tone_map_config },
    .shader_pass_config = observer_ptr { &shader_pass_config },
  };
}

void PrepareView(CompositionViewImpl& view_impl, const CompositionView& desc,
  FakeGraphics& graphics, const ViewId published_view_id)
{
  const auto tag = ViewLifecycleTagFactory::Get();
  view_impl.PrepareForRender(desc, 0U, SequenceNumber { 1 }, graphics, tag);
  view_impl.SetPublishedViewId(published_view_id, tag);
}

TEST(CompositionPlannerTest, PrimarySceneViewUsesCopyTaskAtFullOpacity)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 101U }, MakeView(), oxygen::scene::SceneNode {});
  scene_view.opacity = 1.0F;

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics, ViewId { 201U });

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs());

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 1U);
  EXPECT_EQ(
    submission.tasks[0].type, oxygen::engine::CompositingTaskType::kCopy);
  EXPECT_EQ(submission.tasks[0].copy.source_view_id, ViewId { 201U });
}

TEST(CompositionPlannerTest, PartialOpacitySceneViewUsesTextureBlendTask)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 102U }, MakeView(), oxygen::scene::SceneNode {});
  scene_view.opacity = 0.5F;

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics, ViewId { 202U });

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs());

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 1U);
  EXPECT_EQ(submission.tasks[0].type,
    oxygen::engine::CompositingTaskType::kBlendTexture);
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
  PrepareView(view_impl, overlay_view, *graphics, ViewId { 203U });

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs());

  CompositionPlanner planner(observer_ptr { &builder });
  planner.PlanCompositingTasks();
  const auto submission
    = planner.BuildCompositionSubmission(MakeCompositeTarget(graphics));

  ASSERT_EQ(submission.tasks.size(), 1U);
  EXPECT_EQ(submission.tasks[0].type,
    oxygen::engine::CompositingTaskType::kBlendTexture);
  EXPECT_FLOAT_EQ(submission.tasks[0].texture_blend.alpha, 1.0F);
}

TEST(CompositionPlannerTest, SceneViewPlansDepthPrePassByDefault)
{
  auto graphics = std::make_shared<FakeGraphics>();

  CompositionView scene_view = CompositionView::ForScene(
    ViewId { 104U }, MakeView(), oxygen::scene::SceneNode {});

  CompositionViewImpl view_impl;
  PrepareView(view_impl, scene_view, *graphics, ViewId { 204U });

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs());

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
  PrepareView(view_impl, scene_view, *graphics, ViewId { 205U });

  auto inputs = MakeInputs();
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
  PrepareView(view_impl, scene_view, *graphics, ViewId { 206U });

  FramePlanBuilder builder;
  std::array views { &view_impl };
  builder.BuildFrameViewPackets(observer_ptr<oxygen::scene::Scene> {},
    std::span<CompositionViewImpl* const> { views.data(), views.size() },
    MakeInputs());

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
  PrepareView(view_impl, scene_view, *graphics, ViewId { 207U });

  auto inputs = MakeInputs();
  auto shader_pass_config = oxygen::engine::ShaderPassConfig {};
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

} // namespace
