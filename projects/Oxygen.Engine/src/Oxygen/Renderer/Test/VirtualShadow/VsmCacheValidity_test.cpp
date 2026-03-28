//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmCacheValidityLiveSceneTest : public VsmLiveSceneHarness {
protected:
  static constexpr auto kWidth = 256U;
  static constexpr auto kHeight = 256U;
  static constexpr auto kSlot = Slot { 0U };

  [[nodiscard]] static auto MakeDirectionalView(
    const glm::vec3 offset = glm::vec3 { 0.0F }) -> oxygen::ResolvedView
  {
    return MakeLookAtResolvedView(glm::vec3 { -3.2F, 3.4F, 5.8F } + offset,
      glm::vec3 { 0.2F, 0.8F, 0.0F } + offset, kWidth, kHeight);
  }

  [[nodiscard]] static auto MakeSunDirection() -> glm::vec3
  {
    return glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  }

  static auto DisableDirectionalShadowCasts(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    auto sun_impl = scene_data.sun_node.GetImpl();
    ASSERT_TRUE(sun_impl.has_value());
    auto& sun_light
      = sun_impl->get().GetComponent<oxygen::scene::DirectionalLight>();
    sun_light.Common().casts_shadows = false;
    UpdateTransforms(*scene_data.scene, scene_data.sun_node);
  }

  auto AttachPrimarySpotLight(TwoBoxShadowSceneData& scene_data) -> void
  {
    const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
    AttachSpotLightToTwoBoxScene(scene_data, camera_eye,
      PrimarySpotTargetForTwoBoxScene(scene_data), 18.0F, glm::radians(30.0F),
      glm::radians(50.0F));
  }

  [[nodiscard]] static auto CountMappedPages(
    const oxygen::renderer::vsm::VsmExtractedCacheFrame& snapshot)
    -> std::size_t
  {
    return static_cast<std::size_t>(
      std::count_if(snapshot.page_table.begin(), snapshot.page_table.end(),
        [](const auto& entry) { return entry.is_mapped; }));
  }
};

NOLINT_TEST_F(VsmCacheValidityLiveSceneTest,
  PageRequestBridgeLeavesCacheUnavailableUntilExtractionCompletes)
{
  constexpr auto kSequence = SequenceNumber { 511U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto bridge = RunTwoBoxPageRequestBridge(*renderer, scene, vsm_renderer,
    MakeDirectionalView(), kWidth, kHeight, kSequence, kSlot, 0xE170ULL);

  auto& cache_manager = vsm_renderer.GetCacheManager();
  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kReady);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(cache_manager.IsCacheDataAvailable());
  EXPECT_EQ(cache_manager.GetPreviousFrame(), nullptr);
  ASSERT_NE(cache_manager.GetCurrentFrame(), nullptr);
  EXPECT_TRUE(cache_manager.GetCurrentFrame()->is_ready);
  EXPECT_EQ(cache_manager.GetCurrentFrame()->snapshot.virtual_frame,
    bridge.prepared_products.virtual_frame);
}

NOLINT_TEST_F(VsmCacheValidityLiveSceneTest,
  DirectionalExtractionMarksCacheValidAndEnablesNextFrameReuse)
{
  constexpr auto kFirstSequence = SequenceNumber { 521U };
  constexpr auto kSecondSequence = SequenceNumber { 522U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer,
      MakeDirectionalView(), kWidth, kHeight, kFirstSequence, kSlot, 0xE171ULL);

  auto& cache_manager = vsm_renderer.GetCacheManager();
  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(cache_manager.IsCacheDataAvailable());
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_NE(cache_manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(
    cache_manager.GetPreviousFrame()->frame_generation, kFirstSequence.get());
  EXPECT_EQ(
    cache_manager.GetPreviousFrame()->virtual_frame, first_frame.virtual_frame);
  EXPECT_GT(CountMappedPages(*cache_manager.GetPreviousFrame()), 0U);

  const auto second_bridge = RunTwoBoxPageRequestBridge(*renderer, scene,
    vsm_renderer, MakeDirectionalView(), kWidth, kHeight, kSecondSequence,
    kSlot, 0xE171ULL);

  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kReady);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(cache_manager.IsCacheDataAvailable());
  ASSERT_NE(cache_manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(
    cache_manager.GetPreviousFrame()->frame_generation, kFirstSequence.get());
  EXPECT_GT(second_bridge.committed_frame.plan.reused_page_count, 0U);
  EXPECT_EQ(second_bridge.committed_frame.plan.allocated_page_count, 0U);
}

NOLINT_TEST_F(VsmCacheValidityLiveSceneTest,
  ExplicitInvalidateClearsCacheValidityUntilFreshExtractionRestoresIt)
{
  constexpr auto kFirstSequence = SequenceNumber { 531U };
  constexpr auto kSecondSequence = SequenceNumber { 532U };
  constexpr auto kThirdSequence = SequenceNumber { 533U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer,
      MakeDirectionalView(), kWidth, kHeight, kFirstSequence, kSlot, 0xE172ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);

  auto& cache_manager = vsm_renderer.GetCacheManager();
  cache_manager.InvalidateAll(
    VsmCacheInvalidationReason::kExplicitInvalidateAll);

  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(cache_manager.IsCacheDataAvailable());
  ASSERT_NE(cache_manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(
    cache_manager.GetPreviousFrame()->frame_generation, kFirstSequence.get());

  const auto invalidated_bridge = RunTwoBoxPageRequestBridge(*renderer, scene,
    vsm_renderer, MakeDirectionalView(), kWidth, kHeight, kSecondSequence,
    kSlot, 0xE172ULL);

  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kReady);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(cache_manager.IsCacheDataAvailable());
  EXPECT_EQ(invalidated_bridge.committed_frame.plan.reused_page_count, 0U);
  EXPECT_GT(invalidated_bridge.committed_frame.plan.allocated_page_count, 0U);

  cache_manager.AbortFrame();
  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);

  const auto recovered_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
      MakeDirectionalView(), kWidth, kHeight, kThirdSequence, kSlot, 0xE172ULL);
  ASSERT_NE(recovered_frame.extracted_frame, nullptr);
  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(cache_manager.IsCacheDataAvailable());
  ASSERT_NE(cache_manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(
    cache_manager.GetPreviousFrame()->frame_generation, kThirdSequence.get());
}

NOLINT_TEST_F(VsmCacheValidityLiveSceneTest,
  PagedSpotLightExtractionMarksCacheValidAndEnablesLocalReuse)
{
  constexpr auto kFirstSequence = SequenceNumber { 541U };
  constexpr auto kSecondSequence = SequenceNumber { 542U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 1U);
  DisableDirectionalShadowCasts(scene);
  AttachPrimarySpotLight(scene);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer,
      MakeDirectionalView(), kWidth, kHeight, kFirstSequence, kSlot, 0xE173ULL);

  auto& cache_manager = vsm_renderer.GetCacheManager();
  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(cache_manager.IsCacheDataAvailable());
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_NE(cache_manager.GetPreviousFrame(), nullptr);
  ASSERT_EQ(
    cache_manager.GetPreviousFrame()->virtual_frame.local_light_layouts.size(),
    1U);
  EXPECT_GT(CountMappedPages(*cache_manager.GetPreviousFrame()), 0U);

  const auto second_bridge = RunTwoBoxPageRequestBridge(*renderer, scene,
    vsm_renderer, MakeDirectionalView(), kWidth, kHeight, kSecondSequence,
    kSlot, 0xE173ULL);

  EXPECT_EQ(cache_manager.DescribeBuildState(), VsmCacheBuildState::kReady);
  EXPECT_EQ(
    cache_manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(cache_manager.IsCacheDataAvailable());
  EXPECT_GT(second_bridge.committed_frame.plan.reused_page_count, 0U);
  EXPECT_EQ(second_bridge.committed_frame.plan.allocated_page_count, 0U);
  ASSERT_EQ(
    second_bridge.prepared_products.virtual_frame.local_light_layouts.size(),
    1U);
}

} // namespace
