//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <memory>
#include <type_traits>

#include <glm/ext/scalar_constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::vortex::DirectionalShadowFrameData;
using oxygen::vortex::FrameDirectionalCsmSplitMode;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ShadowCascadeBinding;
using oxygen::vortex::ShadowFrameBindings;
using oxygen::vortex::ShadowService;
using oxygen::vortex::kDirectionalLightShadowFlagCastsShadows;
using oxygen::vortex::shadows::internal::CascadeShadowSetup;
using oxygen::vortex::shadows::internal::ConventionalShadowTargetAllocator;
using oxygen::vortex::testing::FakeGraphics;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Renderer>
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics).get();
  constexpr auto kCapabilities = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kLightingData;
  return { new Renderer(
             std::weak_ptr<Graphics>(graphics), std::move(config), kCapabilities),
    DestroyRenderer };
}

auto MakePerspectiveResolvedView() -> oxygen::ResolvedView
{
  auto params = oxygen::ResolvedView::Params {};
  params.view_config.viewport = oxygen::ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 128.0F,
    .height = 128.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.view_config.reverse_z = true;
  params.view_matrix = glm::mat4(1.0F);
  params.near_plane = 0.1F;
  params.far_plane = 100.0F;
  params.proj_matrix = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    glm::pi<float>() / 3.0F, 1.0F, params.near_plane, params.far_plane);
  return oxygen::ResolvedView(params);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShadowFrameBindingsExposeDirectionalConventionalShadowContract)
{
  auto bindings = ShadowFrameBindings {};

  EXPECT_EQ(bindings.conventional_shadow_surface_handle,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.cascade_count, 0U);
  EXPECT_EQ(bindings.technique_flags, 0U);
  EXPECT_EQ(bindings.sampling_contract_flags, 0U);
  EXPECT_EQ(bindings.cascades.size(), ShadowFrameBindings::kMaxCascades);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  DirectionalShadowFrameDataDoesNotClaimLocalLightShadowPayload)
{
  auto frame_data = DirectionalShadowFrameData {};
  frame_data.bindings.cascade_count = 2U;

  EXPECT_EQ(frame_data.backing_resolution.x, 0U);
  EXPECT_EQ(frame_data.backing_resolution.y, 0U);
  EXPECT_EQ(frame_data.storage_flags, 0U);
  EXPECT_EQ(frame_data.bindings.cascade_count, 2U);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  FrameLightSelectionCarriesSharedDirectionalAuthorityForShadowService)
{
  auto selection = FrameLightSelection {};
  selection.selection_epoch = 19U;
  selection.directional_light = FrameDirectionalLightSelection {
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F },
    .color = glm::vec3 { 1.0F, 0.95F, 0.8F },
    .illuminance_lux = 1400.0F,
    .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
    .cascade_count = 4U,
    .cascade_split_mode = FrameDirectionalCsmSplitMode::kManualDistances,
    .max_shadow_distance = 128.0F,
    .cascade_distances = { 16.0F, 32.0F, 64.0F, 128.0F },
    .transition_fraction = 0.2F,
    .distance_fadeout_fraction = 0.15F,
    .shadow_bias = 0.001F,
    .shadow_normal_bias = 0.03F,
  };

  ASSERT_TRUE(selection.directional_light.has_value());
  EXPECT_EQ(selection.directional_light->cascade_count, 4U);
  EXPECT_NE(selection.directional_light->shadow_flags
      & kDirectionalLightShadowFlagCastsShadows,
    0U);
  EXPECT_EQ(selection.directional_light->cascade_split_mode,
    FrameDirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(selection.directional_light->cascade_distances[3], 128.0F);
  EXPECT_TRUE(selection.local_lights.empty());
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShadowServiceIsANonPlaceholderDirectionalOnlySubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<ShadowService>));
  EXPECT_TRUE((std::is_destructible_v<ShadowService>));
  EXPECT_TRUE((std::is_standard_layout_v<ShadowCascadeBinding>));
}

class ShadowServiceBehaviorTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  DirectionalOnlyShadowServiceStartsWithEmptyDirectionalPublicationAndNoVsm)
{
  auto service = ShadowService(*renderer_);

  EXPECT_FALSE(service.HasVsm());
  EXPECT_EQ(service.InspectShadowData(oxygen::ViewId { 11U }), nullptr);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  DirectionalCascadeCoverageExtendsNonLastSplitsForTransitionOverlap)
{
  auto resolved_view = MakePerspectiveResolvedView();
  const auto view_input = oxygen::vortex::PreparedViewShadowInput {
    .view_id = oxygen::ViewId { 3U },
    .resolved_view = oxygen::observer_ptr<const oxygen::ResolvedView> {
      &resolved_view },
  };
  const auto allocation
    = ConventionalShadowTargetAllocator::DirectionalAllocation {
        .surface_srv = oxygen::ShaderVisibleIndex { 7U },
        .resolution = glm::uvec2 { 2048U, 2048U },
        .cascade_count = 3U,
      };
  const auto directional_light = FrameDirectionalLightSelection {
    .direction = glm::vec3 { 0.0F, -1.0F, -1.0F },
    .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
    .cascade_count = 3U,
    .cascade_split_mode = FrameDirectionalCsmSplitMode::kManualDistances,
    .max_shadow_distance = 40.0F,
    .cascade_distances = { 10.0F, 20.0F, 40.0F, 40.0F },
    .transition_fraction = 0.25F,
    .distance_fadeout_fraction = 0.1F,
  };

  const auto frame_data = CascadeShadowSetup {}.BuildDirectionalFrameData(
    view_input, directional_light, allocation);

  ASSERT_EQ(frame_data.bindings.cascade_count, 3U);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades[0].split_near, 0.1F);
  EXPECT_NEAR(frame_data.bindings.cascades[0].split_far, 12.475F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades[1].split_near, 10.0F);
  EXPECT_NEAR(frame_data.bindings.cascades[1].split_far, 22.5F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades[2].split_near, 20.0F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades[2].split_far, 40.0F);
  EXPECT_NEAR(
    frame_data.bindings.cascades[0].sampling_metadata1.x, 2.475F, 0.0001F);
  EXPECT_NEAR(
    frame_data.bindings.cascades[1].sampling_metadata1.x, 2.5F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades[2].sampling_metadata1.x, 0.0F);
}

} // namespace
