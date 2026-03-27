//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmBeginFrameLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmBeginFrameLiveSceneTest,
  NewFrameStartClearsPreparedViewButPreservesExtractedRealSceneHistory)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kFirstSlot = Slot { 0U };
  constexpr auto kSecondSlot = Slot { 1U };
  constexpr auto kFirstSequence = SequenceNumber { 31U };
  constexpr auto kSecondSequence = SequenceNumber { 32U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kFirstSlot, 0x1001ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  EXPECT_EQ(
    first_frame.extracted_frame->frame_generation, kFirstSequence.get());

  vsm_renderer.OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(), kSecondSequence,
    kSecondSlot);

  EXPECT_EQ(vsm_renderer.TryGetPreparedViewState(kTestViewId), nullptr);
  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kIdle);
  EXPECT_EQ(vsm_renderer.GetCacheManager().GetCurrentFrame(), nullptr);
  ASSERT_NE(vsm_renderer.GetCacheManager().GetPreviousFrame(), nullptr);
  EXPECT_EQ(vsm_renderer.GetCacheManager().GetPreviousFrame()->frame_generation,
    kFirstSequence.get());
  EXPECT_EQ(vsm_renderer.GetCacheManager().GetPreviousFrame()->virtual_frame,
    first_frame.virtual_frame);

  const auto* prepared_view
    = PrepareTwoBoxView(*renderer, scene, vsm_renderer, resolved_view,
      kSecondSequence, kSecondSlot, static_cast<float>(kWidth), 0x1002ULL);
  ASSERT_NE(prepared_view, nullptr);
  EXPECT_EQ(prepared_view->frame_sequence, kSecondSequence);
  EXPECT_EQ(prepared_view->frame_slot, kSecondSlot);
  EXPECT_EQ(prepared_view->active_scene.get(), scene.scene.get());
  EXPECT_TRUE(prepared_view->has_virtual_shadow_work);
  EXPECT_EQ(prepared_view->directional_shadow_candidates.size(), 1U);
  EXPECT_EQ(prepared_view->scene_primitive_history.size(), 2U);
  EXPECT_EQ(prepared_view->shadow_caster_bounds.size(), 2U);
  EXPECT_EQ(prepared_view->visible_receiver_bounds.size(), 1U);
}

} // namespace
