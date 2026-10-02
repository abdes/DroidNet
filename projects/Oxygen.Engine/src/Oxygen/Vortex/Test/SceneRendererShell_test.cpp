//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Fakes/Graphics.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestEngine.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace {

using oxygen::Graphics;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::CompositionView;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneRenderBuilder;
using oxygen::vortex::SceneRenderer;
using oxygen::vortex::ShadingMode;
using oxygen::vortex::testing::FakeGraphics;

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
  composition_view.id = ViewId {
    7U,
  };
  composition_view.view = view;
  composition_view.shading_mode = ShadingMode::kForward;
  return composition_view;
}

auto MakeFrameView(const float width, const float height,
  const bool is_scene_view) -> oxygen::engine::ViewContext
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
  const CapabilitySet capabilities = CapabilitySet {})
  -> std::shared_ptr<Renderer>
{
  auto config = oxygen::RendererConfig {};
  config.upload_queue_key
    = graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics).get();
  return {
    new Renderer(
      std::weak_ptr<Graphics>(graphics), std::move(config), capabilities),
    DestroyRenderer,
  };
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
    = oxygen::observer_ptr<const CompositionView> {
        &composition_view,
      };

  ASSERT_NE(context.GetCurrentCompositionView(), nullptr);
  EXPECT_EQ(context.GetCurrentCompositionView()->id, composition_view.id);
  const auto shading_mode
    = context.GetCurrentCompositionView()->GetShadingMode();
  if (!shading_mode.has_value()) {
    FAIL() << "Expected shading_mode to have a value";
  }
  EXPECT_EQ(*shading_mode, ShadingMode::kForward);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  BuilderCreatesShellForEmptyCapabilitiesAndPreservesNonZeroExtent)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);

  auto scene_renderer
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {},
      {
        640U,
        480U,
      });

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
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {},
      {
        0U,
        0U,
      });

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().x, 1U);
  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent().y, 1U);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  DeferredCapabilityControlsTheBuilderDefaultShadingMode)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer
    = MakeRenderer(graphics, RendererCapabilityFamily::kDeferredShading);

  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading,
    {
      320U,
      180U,
    });

  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_EQ(scene_renderer->GetDefaultShadingMode(), ShadingMode::kDeferred);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  PerViewShadingIntentOverridesTheBootstrapDefaultWhenAViewIsBound)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer
    = MakeRenderer(graphics, RendererCapabilityFamily::kDeferredShading);

  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading,
    {
      320U,
      180U,
    });
  auto context = RenderContext {};
  auto composition_view = MakeSceneView();

  ASSERT_NE(scene_renderer, nullptr);
  context.current_view.composition_view
    = oxygen::observer_ptr<const CompositionView> {
        &composition_view,
      };

  EXPECT_EQ(
    scene_renderer->GetEffectiveShadingMode(context), ShadingMode::kForward);
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  RenderContextShadingOverrideSurvivesWithoutALiveCompositionViewObject)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer
    = MakeRenderer(graphics, RendererCapabilityFamily::kDeferredShading);

  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading,
    {
      320U,
      180U,
    });
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
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {},
      {
        1280U,
        720U,
      });

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
  const auto renderer
    = MakeRenderer(graphics, RendererCapabilityFamily::kDeferredShading);
  auto scene_renderer = SceneRenderBuilder::Build(*renderer, *graphics,
    RendererCapabilityFamily::kDeferredShading,
    {
      640U,
      360U,
    });
  auto frame_context = oxygen::engine::FrameContext {};
  auto render_context = RenderContext {};

  const SceneRenderer::StageOrder expected_stage_order {
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
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
  FrameStartUsesTheLargestSceneViewEnvelopeInsteadOfTheFirstValidViewport)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics);
  auto scene_renderer
    = SceneRenderBuilder::Build(*renderer, *graphics, CapabilitySet {},
      {
        1U,
        1U,
      });
  auto frame_context = oxygen::engine::FrameContext {};

  std::ignore = frame_context.RegisterView(MakeFrameView(48.0F, 48.0F, false));
  std::ignore = frame_context.RegisterView(MakeFrameView(64.0F, 64.0F, true));
  std::ignore = frame_context.RegisterView(MakeFrameView(320.0F, 180.0F, true));

  ASSERT_NE(scene_renderer, nullptr);
  scene_renderer->OnFrameStart(frame_context);

  EXPECT_EQ(scene_renderer->GetSceneTextures().GetExtent(),
    (glm::uvec2 {
      320U,
      180U,
    }));
}

NOLINT_TEST(SceneRendererShellProofSurfaceTest,
  NoViewMaintenanceReclaimsGeometryOncePerFrameWithoutSubmission)
{
  using oxygen::observer_ptr;
  using oxygen::frame::SequenceNumber;
  using oxygen::frame::Slot;
  using oxygen::vortex::testing::FakeAssetLoader;
  using oxygen::vortex::testing::exposure::ExposureTestEngine;
  constexpr CapabilitySet kCapabilities
    = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kGpuUploadAndAssetBinding;
  constexpr auto kLodBudget
    = oxygen::vortex::resources::GeometryUploader::MaintenanceLimits {}
        .max_reclaimed_lods_per_frame;
  constexpr std::size_t kAssetCount = kLodBudget + 1U;
  constexpr std::size_t kBuffersPerAsset = 2U;

  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  FakeAssetLoader loader;
  ::testing::NiceMock<ExposureTestEngine> engine;
  ON_CALL(engine, GetAssetLoader())
    .WillByDefault(::testing::Return(
      observer_ptr<oxygen::content::IAssetLoader> { &loader }));
  const auto renderer = MakeRenderer(graphics, kCapabilities);
  ASSERT_TRUE(
    renderer->OnAttached(observer_ptr<oxygen::IAsyncEngine> { &engine }));
  oxygen::engine::FrameContext frame;
  const auto begin_renderer_frame = [&](SequenceNumber sequence, Slot slot) {
    frame.SetCurrentPhase(oxygen::core::PhaseId::kFrameStart,
      oxygen::engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      sequence, oxygen::engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, oxygen::engine::internal::EngineTagFactory::Get());
    auto timing = oxygen::engine::ModuleTimingData {};
    timing.game_delta_time = oxygen::time::SimulationClock::kMinDeltaTime;
    frame.SetModuleTimingData(
      timing, oxygen::engine::internal::EngineTagFactory::Get());
    renderer->OnFrameStart(
      observer_ptr<oxygen::engine::FrameContext> { &frame });
  };
  begin_renderer_frame(SequenceNumber { 1U }, Slot { 0U });
  auto scene_renderer = SceneRenderBuilder::Build(
    *renderer, *graphics, kCapabilities, { 64U, 64U });
  scene_renderer->OnFrameStart(frame);

  std::array<oxygen::data::Vertex, 3> vertices {};
  vertices.at(0).position = { -0.25F, -0.25F, 0.5F };
  vertices.at(1).position = { 0.25F, -0.25F, 0.5F };
  vertices.at(2).position = { 0.0F, 0.25F, 0.5F };
  for (auto& vertex : vertices) {
    vertex.normal = { 0.0F, 0.0F, 1.0F };
    vertex.tangent = { 1.0F, 0.0F, 0.0F };
    vertex.bitangent = { 0.0F, 1.0F, 0.0F };
    vertex.color = { 1.0F, 1.0F, 1.0F, 1.0F };
  }
  const auto texture_key = loader.PreloadCookedTexture(
    oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload());
  auto material_description = oxygen::data::pak::render::MaterialAssetDesc {};
  const auto material = std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey::FromVirtualPath("/Test/Maintenance/Surface.omat"),
    material_description);
  material->SetTextureResourceKeys({ texture_key });
  const std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::MeshBuilder(0U, "Maintenance triangle")
        .WithVertices(vertices)
        .WithIndices(std::vector<std::uint32_t> { 0U, 1U, 2U })
        .BeginSubMesh("Surface", material)
        .WithMeshView({ .first_index = 0U,
          .index_count = 3U,
          .first_vertex = 0U,
          .vertex_count = 3U })
        .EndSubMesh()
        .Build();
  auto scene = std::make_shared<oxygen::scene::Scene>(
    "No-view maintenance", kAssetCount + 1U);
  std::vector<oxygen::data::AssetKey> keys;
  keys.reserve(kAssetCount);
  for (std::size_t index = 0U; index < kAssetCount; ++index) {
    keys.push_back(oxygen::data::AssetKey::FromVirtualPath(
      "/Test/Maintenance/" + std::to_string(index) + ".ogeo"));
    oxygen::data::pak::geometry::GeometryAssetDesc description {};
    description.lod_count = 1U;
    std::ranges::copy(
      std::array { -0.25F, -0.25F, 0.5F }, description.bounding_box_min);
    std::ranges::copy(
      std::array { 0.25F, 0.25F, 0.5F }, description.bounding_box_max);
    auto node = scene->CreateNode("Resident-" + std::to_string(index));
    ASSERT_TRUE(node.IsAlive());
    node.GetRenderable().SetGeometry(
      std::make_shared<oxygen::data::GeometryAsset>(keys.back(), description,
        std::vector<std::shared_ptr<oxygen::data::Mesh>> { mesh }));
  }
  scene->Update();
  auto parameters = oxygen::ResolvedView::Params {};
  parameters.view_config.viewport = ViewPort { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 64.0F,
    .height = 64.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F };
  parameters.near_plane = 0.0F;
  parameters.far_plane = 1.0F;
  const auto resolved = oxygen::ResolvedView(parameters);
  auto context = RenderContext {};
  context.scene = observer_ptr<oxygen::scene::Scene> { scene.get() };
  context.frame_sequence = SequenceNumber { 1U };
  context.frame_slot = Slot { 0U };
  auto& view = context.frame_views.emplace_back();
  view.view_id = ViewId { 7U };
  view.is_scene_view = true;
  view.resolved_view = observer_ptr<const oxygen::ResolvedView> { &resolved };
  scene_renderer->PrimePreparedViews(context);
  ASSERT_GE(
    graphics->buffer_log_.copies.size(), kAssetCount * kBuffersPerAsset);
  auto& registry = graphics->GetResourceRegistry();
  const auto before = registry.GetRegisteredResourceCount();
  ASSERT_GE(before, kAssetCount * kBuffersPerAsset);
  const auto queue
    = graphics->GetFakeCommandQueue(oxygen::graphics::QueueRole::kGraphics);
  ASSERT_NE(queue, nullptr);
  const auto submissions = queue->submitted_batches;
  const auto copies = graphics->buffer_log_.copies.size();
  context.frame_views.clear();
  for (const auto key : keys) {
    loader.EmitGeometryAssetEviction(
      key, oxygen::content::EvictionReason::kClear);
  }
  begin_renderer_frame(SequenceNumber { 2U }, Slot { 1U });
  scene_renderer->OnFrameStart(frame);
  const auto after_first = before - kLodBudget * kBuffersPerAsset;
  EXPECT_EQ(registry.GetRegisteredResourceCount(), after_first);
  EXPECT_EQ(queue->submitted_batches, submissions);
  scene_renderer->OnStandaloneFrameStart(
    SequenceNumber { 2U }, Slot { 1U }, std::nullopt);
  context.frame_sequence = SequenceNumber { 2U };
  context.frame_slot = Slot { 1U };
  scene_renderer->PrimePreparedViews(context);
  scene_renderer->OnFrameStart(frame);
  EXPECT_EQ(registry.GetRegisteredResourceCount(), after_first);
  EXPECT_EQ(queue->submitted_batches, submissions);
  context.scene.reset();
  scene.reset();
  begin_renderer_frame(SequenceNumber { 3U }, Slot { 2U });
  scene_renderer->OnFrameStart(frame);
  EXPECT_EQ(registry.GetRegisteredResourceCount(),
    before - kAssetCount * kBuffersPerAsset);
  EXPECT_EQ(queue->submitted_batches, submissions);
  EXPECT_EQ(graphics->buffer_log_.copies.size(), copies);
}

} // namespace
