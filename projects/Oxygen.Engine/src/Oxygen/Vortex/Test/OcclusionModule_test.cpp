//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
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

auto MakeConfig(FakeGraphics& graphics) -> RendererConfig
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics.QueueKeyFor(QueueRole::kGraphics).get();
  return config;
}

auto MakeRenderer(std::shared_ptr<FakeGraphics> graphics)
  -> std::shared_ptr<Renderer>
{
  return std::shared_ptr<Renderer>(
    new Renderer(std::weak_ptr<Graphics>(graphics), MakeConfig(*graphics),
      RendererCapabilityFamily::kScenePreparation
        | RendererCapabilityFamily::kDeferredShading),
    [](Renderer* renderer) {
      if (renderer != nullptr) {
        renderer->OnShutdown();
        delete renderer;
      }
    });
}

struct OcclusionModuleFixture {
  std::shared_ptr<FakeGraphics> graphics { std::make_shared<FakeGraphics>() };
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

auto MakePreparedFrame(
  std::vector<oxygen::vortex::DrawMetadata>& metadata) -> PreparedSceneFrame
{
  auto frame = PreparedSceneFrame {};
  frame.draw_metadata_bytes = std::as_bytes(std::span { metadata });
  return frame;
}

NOLINT_TEST(OcclusionTypesTest, FallbackReasonStringsAreStable)
{
  EXPECT_EQ(
    oxygen::vortex::to_string(OcclusionFallbackReason::kNone), "None");
  EXPECT_EQ(oxygen::vortex::to_string(
              OcclusionFallbackReason::kNoCurrentFurthestHzb),
    "NoCurrentFurthestHzb");
  EXPECT_EQ(oxygen::vortex::to_string(
              OcclusionFallbackReason::kCapacityOverflow),
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
  auto module = OcclusionModule { *fixture.renderer };
  auto ctx = RenderContext {};

  module.Execute(ctx, *fixture.scene_textures);

  ASSERT_NE(ctx.current_view.occlusion_results.get(), nullptr);
  EXPECT_FALSE(ctx.current_view.occlusion_results->valid);
  EXPECT_TRUE(ctx.current_view.occlusion_results->IsDrawVisible(7U));
  EXPECT_EQ(module.GetStats().fallback_reason,
    OcclusionFallbackReason::kStageDisabled);
}

NOLINT_TEST(OcclusionModuleTest, EnabledStageWithoutPreparedFrameStaysInvalid)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule { *fixture.renderer,
    OcclusionConfig { .enabled = true } };
  auto ctx = RenderContext {};

  module.Execute(ctx, *fixture.scene_textures);

  ASSERT_NE(ctx.current_view.occlusion_results.get(), nullptr);
  EXPECT_FALSE(ctx.current_view.occlusion_results->valid);
  EXPECT_EQ(module.GetStats().fallback_reason,
    OcclusionFallbackReason::kNoPreparedFrame);
}

NOLINT_TEST(OcclusionModuleTest, MissingCurrentFurthestHzbPublishesAllVisible)
{
  auto fixture = OcclusionModuleFixture {};
  auto module = OcclusionModule { *fixture.renderer,
    OcclusionConfig { .enabled = true } };
  auto metadata = std::vector<oxygen::vortex::DrawMetadata>(3U);
  auto prepared_frame = MakePreparedFrame(metadata);
  auto ctx = RenderContext {};
  ctx.current_view.prepared_frame
    = oxygen::observer_ptr<const PreparedSceneFrame> { &prepared_frame };

  module.Execute(ctx, *fixture.scene_textures);

  const auto& results = module.GetCurrentResults();
  EXPECT_TRUE(results.valid);
  EXPECT_EQ(results.draw_count, 3U);
  EXPECT_EQ(results.visible_by_draw.size(), 3U);
  EXPECT_TRUE(results.IsDrawVisible(0U));
  EXPECT_TRUE(results.IsDrawVisible(2U));
  EXPECT_TRUE(results.IsDrawVisible(3U));
  EXPECT_EQ(results.fallback_reason,
    OcclusionFallbackReason::kNoCurrentFurthestHzb);
  EXPECT_EQ(module.GetStats().visible_count, 3U);
  EXPECT_EQ(module.GetStats().occluded_count, 0U);
  EXPECT_FALSE(module.GetStats().current_furthest_hzb_available);
  ASSERT_NE(ctx.current_view.occlusion_results.get(), nullptr);
  EXPECT_EQ(ctx.current_view.occlusion_results.get(), &results);
}

} // namespace
