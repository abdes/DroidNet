//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::Graphics;
using oxygen::vortex::CompositionView;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneRenderBuilder;
using oxygen::vortex::SceneRenderer;
using oxygen::vortex::ShadingMode;
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

auto MakeSceneView() -> CompositionView
{
  auto view = oxygen::View {};
  view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 640.0F,
    .height = 480.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  auto composition_view = CompositionView {};
  composition_view.id = ViewId { 7U };
  composition_view.view = view;
  composition_view.shading_mode = ShadingMode::kForward;
  return composition_view;
}

auto MakeFrameView(const float width, const float height, const bool is_scene_view)
  -> oxygen::engine::ViewContext
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
  view.metadata.name = is_scene_view ? "Scene" : "Overlay";
  view.metadata.purpose = is_scene_view ? "scene" : "overlay";
  view.metadata.is_scene_view = is_scene_view;
  return view;
}

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics,
  const CapabilitySet capabilities = CapabilitySet {}) -> std::shared_ptr<Renderer>
{
  auto config = oxygen::RendererConfig {};
  config.upload_queue_key
    = graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics).get();
  return { new Renderer(
             std::weak_ptr<Graphics>(graphics), std::move(config), capabilities),
    DestroyRenderer };
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  ActiveCompositionViewSeamDefaultsToNullWhenNoViewIsBound)
{
  const auto context = RenderContext {};

  EXPECT_EQ(context.GetCurrentCompositionView(), nullptr);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  ActiveCompositionViewSeamExposesPerViewShadingIntent)
{
  auto context = RenderContext {};
  auto composition_view = MakeSceneView();
  context.current_view.composition_view
    = oxygen::observer_ptr<const CompositionView> { &composition_view };

  ASSERT_NE(context.GetCurrentCompositionView(), nullptr);
  EXPECT_EQ(context.GetCurrentCompositionView()->id, composition_view.id);
  const auto shading_mode = context.GetCurrentCompositionView()->GetShadingMode();
  ASSERT_TRUE(shading_mode.has_value());
  EXPECT_EQ(*shading_mode, ShadingMode::kForward);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  BuilderCreatesShellForEmptyCapabilitiesAndPreservesNonZeroExtent)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);

  auto scene_renderer
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {}, { 640U, 480U });

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().x, 640U);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().y, 480U);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  BuilderClampsZeroBootstrapExtentBeforeSceneTexturesAllocation)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);

  auto scene_renderer
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {}, { 0U, 0U });

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().x, 1U);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().y, 1U);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  DeferredCapabilityControlsTheBuilderDefaultShadingMode)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics,
    RendererCapabilityFamily::kDeferredShading);

  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading, { 320U, 180U });

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(scene_renderer->GetDefaultShadingMode(), ShadingMode::kDeferred);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  PerViewShadingIntentOverridesTheBootstrapDefaultWhenAViewIsBound)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics,
    RendererCapabilityFamily::kDeferredShading);

  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading, { 320U, 180U });
  auto context = RenderContext {};
  auto composition_view = MakeSceneView();

  ASSERT_NE(scene_renderer, nullptr);
  context.current_view.composition_view
    = oxygen::observer_ptr<const CompositionView> { &composition_view };

  EXPECT_EQ(
    scene_renderer->GetEffectiveShadingMode(context), ShadingMode::kForward);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  RenderContextShadingOverrideSurvivesWithoutALiveCompositionViewObject)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics,
    RendererCapabilityFamily::kDeferredShading);

  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading, { 320U, 180U });
  auto context = RenderContext {};

  ASSERT_NE(scene_renderer, nullptr);
  context.current_view.shading_mode_override = ShadingMode::kForward;

  EXPECT_EQ(
    scene_renderer->GetEffectiveShadingMode(context), ShadingMode::kForward);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  BuilderBootstrapContractIsSeededForEmptyCapabilitiesAndNonZeroExtent)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);

  auto scene_renderer
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {}, { 1280U, 720U });

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(scene_renderer->GetDefaultShadingMode(), ShadingMode::kForward);
  ASSERT_NE(scene_renderer->GetSceneTextures().GetVelocity(), nullptr);
  ASSERT_NE(scene_renderer->GetSceneTextures().GetCustomDepth(), nullptr);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().x, 1280U);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().y, 720U);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  TwentyThreeStageNullSafeDispatchContractIsSeededForWave0)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics,
    RendererCapabilityFamily::kDeferredShading);
  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading, { 640U, 360U });
  auto frame_context = oxygen::engine::FrameContext {};
  auto render_context = RenderContext {};

  const SceneRenderer::StageOrder expected_stage_order {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23,
  };

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(SceneRenderer::GetAuthoredStageOrder(), expected_stage_order);
  EXPECT_NO_THROW(scene_renderer->OnFrameStart(frame_context));
  EXPECT_NO_THROW(scene_renderer->OnPreRender(frame_context));
  EXPECT_NO_THROW(scene_renderer->OnRender(render_context));
  EXPECT_NO_THROW(scene_renderer->OnCompositing(render_context));
  EXPECT_NO_THROW(scene_renderer->OnFrameEnd(frame_context));
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  Stage22StaysBetweenResolveAndCleanupOwnershipBoundaries)
{
  const auto source_root = SourceRoot();
  const auto scene_renderer_source
    = ReadTextFile(source_root / "Vortex/SceneRenderer/SceneRenderer.cpp");

  const auto resolve_pos = scene_renderer_source.find("ResolveSceneColor(ctx);");
  const auto post_process_pos
    = scene_renderer_source.find("post_process_->Execute(");
  const auto cleanup_pos = scene_renderer_source.find("PostRenderCleanup(ctx);");

  ASSERT_NE(resolve_pos, std::string::npos);
  ASSERT_NE(post_process_pos, std::string::npos);
  ASSERT_NE(cleanup_pos, std::string::npos);
  EXPECT_LT(resolve_pos, post_process_pos);
  EXPECT_LT(post_process_pos, cleanup_pos);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  Stage13And14RemainReservedAheadOfStage15Activation)
{
  const auto source_root = SourceRoot();
  const auto scene_renderer_source
    = ReadTextFile(source_root / "Vortex/SceneRenderer/SceneRenderer.cpp");

  const auto stage12_pos
    = scene_renderer_source.find("// Stage 12: Deferred direct lighting");
  const auto stage13_pos
    = scene_renderer_source.find("// Stage 13: reserved - IndirectLightingService");
  const auto stage14_pos = scene_renderer_source.find(
    "// Stage 14: reserved - EnvironmentLightingService volumetrics");
  const auto stage15_pos
    = scene_renderer_source.find("// Stage 15: Sky / atmosphere / fog");

  ASSERT_NE(stage12_pos, std::string::npos);
  ASSERT_NE(stage13_pos, std::string::npos);
  ASSERT_NE(stage14_pos, std::string::npos);
  ASSERT_NE(stage15_pos, std::string::npos);
  EXPECT_LT(stage12_pos, stage13_pos);
  EXPECT_LT(stage13_pos, stage14_pos);
  EXPECT_LT(stage14_pos, stage15_pos);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  ResolveAndCleanupSourcesCarryRetainedStageOwnerMarkers)
{
  const auto source_root = SourceRoot();
  const auto resolve_source
    = ReadTextFile(source_root / "Vortex/SceneRenderer/ResolveSceneColor.cpp");
  const auto cleanup_source
    = ReadTextFile(source_root / "Vortex/SceneRenderer/PostRenderCleanup.cpp");

  EXPECT_TRUE(resolve_source.contains("Stage 21 owner"));
  EXPECT_TRUE(cleanup_source.contains("Stage 23 extraction/handoff owner"));
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  FrameStartUsesTheLargestSceneViewEnvelopeInsteadOfTheFirstValidViewport)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);
  auto scene_renderer
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {}, { 1U, 1U });
  auto frame_context = oxygen::engine::FrameContext {};

  static_cast<void>(
    frame_context.RegisterView(MakeFrameView(48.0F, 48.0F, false)));
  static_cast<void>(
    frame_context.RegisterView(MakeFrameView(64.0F, 64.0F, true)));
  static_cast<void>(
    frame_context.RegisterView(MakeFrameView(320.0F, 180.0F, true)));

  ASSERT_NE(scene_renderer, nullptr);
  scene_renderer->OnFrameStart(frame_context);

  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent(),
    (glm::uvec2 { 320U, 180U }));
}

} // namespace
