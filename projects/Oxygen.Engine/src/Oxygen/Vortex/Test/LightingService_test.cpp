//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace {

using oxygen::Graphics;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::ViewPort;
using oxygen::vortex::DirectionalLightForwardData;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightingInputs;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::FrameLocalLightSelection;
using oxygen::vortex::LightingFrameBindings;
using oxygen::vortex::LightingService;
using oxygen::vortex::LocalLightKind;
using oxygen::vortex::PreparedViewLightingInput;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
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
  return {
    new Renderer(
      std::weak_ptr<Graphics>(graphics), std::move(config), kCapabilities),
    DestroyRenderer,
  };
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

NOLINT_TEST(LightingServiceSurfaceTest,
  LightingFrameBindingsExposeDirectionalAndClusterPublicationFields)
{
  auto bindings = LightingFrameBindings {};

  EXPECT_EQ(bindings.local_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.local_indices_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.grid_metadata_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.cluster_ranges_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.directional_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.directional_count, 0U);
  EXPECT_EQ(bindings.local_count, 0U);
  EXPECT_EQ(
    bindings.publication_state, oxygen::vortex::kLightingPublicationDisabled);
  EXPECT_EQ(bindings.build_status_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.local_shadow_map_srv, kInvalidShaderVisibleIndex);
  const auto directional = DirectionalLightForwardData {};
  EXPECT_EQ(directional.atmosphere_light_slot,
    oxygen::vortex::kInvalidAtmosphereLightIndex);
  EXPECT_EQ(directional.ground_transmittance_rgb, glm::vec3 { 1.0F });
}

NOLINT_TEST(LightingServiceSurfaceTest,
  FrameLightSelectionCarriesSharedDirectionalAndLocalLightAuthority)
{
  auto selection = FrameLightSelection {};
  selection.selection_epoch = 77U;
  selection.directional_lights = { FrameDirectionalLightSelection{ .source_node = {},
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F, },
    .color = glm::vec3 { 1.0F, 0.9F, 0.8F, },
    .illuminance_lux = 1200.0F,
    .transmittance_toward_sun_rgb = glm::vec3 { 0.4F, 0.5F, 0.6F, },
    .atmosphere_light_slot = 0U,
    .atmosphere_mode_flags
    = oxygen::vortex::kDirectionalLightAtmosphereModeFlagAuthority
      | oxygen::vortex::
        kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance,
  }, };
  selection.local_lights.push_back(FrameLocalLightSelection {
    .kind = LocalLightKind::kPoint,
    .position = glm::vec3 { 1.0F, 2.0F, 3.0F, },
    .range = 6.0F,
    .color = glm::vec3 { 0.4F, 0.6F, 0.9F, },
    .luminous_flux_lm = 80.0F,
  });

  if (selection.directional_lights.empty()) {

    FAIL() << "Expected a selected directional light";
  }
  EXPECT_EQ(selection.selection_epoch, 77U);
  EXPECT_EQ(selection.local_lights.size(), 1U);
  EXPECT_EQ(selection.local_lights.front().kind, LocalLightKind::kPoint);
  EXPECT_EQ(selection.directional_lights.front().illuminance_lux, 1200.0F);
  EXPECT_EQ(selection.directional_lights.front().atmosphere_light_slot, 0U);
  EXPECT_EQ(selection.directional_lights.front().transmittance_toward_sun_rgb,
    glm::vec3(0.4F, 0.5F, 0.6F));
}

NOLINT_TEST(
  LightingServiceSurfaceTest, LightingServiceIsANonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<oxygen::vortex::LightingService>));
  EXPECT_TRUE((std::is_destructible_v<oxygen::vortex::LightingService>));
  EXPECT_TRUE((std::is_standard_layout_v<DirectionalLightForwardData>));
}

class LightingServiceBehaviorTest : public ::testing::Test {
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

NOLINT_TEST_F(LightingServiceBehaviorTest,
  GridMetadataPreservesViewRectangleAndOrthographicSignedNear)
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = ViewPort {
    .top_left_x = 13.0F,
    .top_left_y = 7.0F,
    .width = 63.0F,
    .height = 65.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.near_plane = -8.0F;
  params.far_plane = 24.0F;
  params.proj_matrix = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
    -2.0F, 2.0F, -2.0F, 2.0F, params.near_plane, params.far_plane);
  const auto view = ResolvedView(params);
  const auto views = std::array {
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 1U },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr { &view },
      .composition_view = {},
    },
  };
  const auto selection = FrameLightSelection {};
  auto builder = oxygen::vortex::lighting::internal::LightGridBuilder {};
  const auto result = builder.Build({
    .frame_light_set = &selection,
    .active_views = views,
  });
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->per_view.size(), 1U);
  const auto& metadata = result->per_view.front().metadata;
  EXPECT_EQ(metadata.grid_size, (glm::uvec3 { 1U, 2U, 32U }));
  EXPECT_EQ(metadata.content_origin_px, (glm::vec2 { 13.0F, 7.0F }));
  EXPECT_EQ(metadata.content_extent_px, (glm::vec2 { 63.0F, 65.0F }));
  EXPECT_EQ(metadata.projection_kind, oxygen::vortex::kLightGridOrthographic);
  EXPECT_EQ(metadata.grid_z_params, glm::vec3 { 0.0F });
  EXPECT_EQ(metadata.near_depth_m, -8.0F);
  EXPECT_EQ(metadata.far_depth_m, 24.0F);
}

NOLINT_TEST_F(LightingServiceBehaviorTest,
  BuildLightGridPublishesSharedBuffersForEveryActiveViewOncePerFrame)
{
  auto service = LightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      8U,
    },
    oxygen::frame::Slot {
      1U,
    });

  auto selection = FrameLightSelection {};
  selection.selection_epoch = 91U;
  selection.directional_lights = { FrameDirectionalLightSelection{ .source_node = {},
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F, },
    .source_radius = 0.05F,
    .color = glm::vec3 { 1.0F, 0.95F, 0.8F, },
    .illuminance_lux = 1600.0F,
    .transmittance_toward_sun_rgb = glm::vec3 { 0.25F, 0.5F, 0.75F, },
    .atmosphere_light_slot = 0U,
    .atmosphere_mode_flags
    = oxygen::vortex::kDirectionalLightAtmosphereModeFlagAuthority
      | oxygen::vortex::
        kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance,
  }, };
  selection.local_lights.push_back(FrameLocalLightSelection {
    .kind = LocalLightKind::kPoint,
    .position = glm::vec3 { 1.0F, 0.0F, 0.0F, },
    .range = 6.0F,
    .color = glm::vec3 { 0.4F, 0.7F, 1.0F, },
    .luminous_flux_lm = 80.0F,
  });
  selection.local_lights.push_back(FrameLocalLightSelection {
    .kind = LocalLightKind::kSpot,
    .position = glm::vec3 { -2.0F, 3.0F, 1.0F, },
    .range = 8.0F,
    .color = glm::vec3 { 1.0F, 0.6F, 0.3F, },
    .luminous_flux_lm = 120.0F,
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F, },
    .inner_cone_half_angle_radians = std::acos(0.95F),
    .outer_cone_half_angle_radians = std::acos(0.75F),
  });

  auto first_view = MakeResolvedView(64.0F, 64.0F);
  auto second_view = MakeResolvedView(96.0F, 54.0F);
  const auto view_inputs = std::array {
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 11U, },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr<const ResolvedView> { &first_view, },
      .composition_view = {},
    },
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 22U, },
      .prepared_scene = {},
      .resolved_view
      = oxygen::observer_ptr<const ResolvedView> { &second_view, },
      .composition_view = {},
    },
  };

  ASSERT_TRUE(service
      .BuildLightGrid(FrameLightingInputs {
        .frame_light_set = &selection,
        .active_views = std::span(view_inputs),
      })
      .has_value());

  const auto& state = service.GetLastGridBuildState();
  EXPECT_EQ(state.build_count, 1U);
  EXPECT_EQ(state.published_view_count, 2U);
  EXPECT_EQ(state.directional_light_count, 1U);
  EXPECT_EQ(state.local_light_count, 2U);
  EXPECT_EQ(state.selection_epoch, 91U);

  const auto* first_bindings
    = service.InspectForwardLightBindings(oxygen::ViewId {
      11U,
    });
  const auto* second_bindings
    = service.InspectForwardLightBindings(oxygen::ViewId {
      22U,
    });
  ASSERT_NE(first_bindings, nullptr);
  ASSERT_NE(second_bindings, nullptr);
  EXPECT_NE(first_bindings->local_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(first_bindings->grid_metadata_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(first_bindings->cluster_ranges_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(first_bindings->local_indices_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(
    first_bindings->directional_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->local_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->grid_metadata_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->cluster_ranges_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(second_bindings->local_indices_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(
    second_bindings->directional_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(first_bindings->directional_records_srv,
    second_bindings->directional_records_srv);
  EXPECT_EQ(
    first_bindings->local_records_srv, second_bindings->local_records_srv);
  EXPECT_EQ(first_bindings->directional_count, 1U);
  EXPECT_EQ(first_bindings->local_count, 2U);
  EXPECT_EQ(first_bindings->publication_state,
    oxygen::vortex::kLightingPublicationRecorded);
  EXPECT_NE(first_bindings->build_status_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(first_bindings->local_shadow_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(first_bindings->index_capacity, 0U);
  EXPECT_EQ(first_bindings->selection_revision.at(0), 91U);
  EXPECT_NE(first_bindings->view_generation, second_bindings->view_generation);
  EXPECT_NE(first_bindings->view_generation, (std::array<std::uint32_t, 2> {}));
}

NOLINT_TEST_F(
  LightingServiceBehaviorTest, FailedPreparationInvalidatesPriorPublication)
{
  auto service = LightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  const auto view = MakeResolvedView(64.0F, 64.0F);
  const auto views = std::array {
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 1U },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr { &view },
      .composition_view = {},
    },
  };
  auto selection = FrameLightSelection {};
  selection.local_lights.push_back(FrameLocalLightSelection {
    .range = 4.0F,
    .luminous_flux_lm = 1000.0F,
  });
  const auto inputs = FrameLightingInputs {
    .frame_light_set = &selection,
    .active_views = views,
  };
  ASSERT_TRUE(service.BuildLightGrid(inputs).has_value());
  ASSERT_NE(
    service.InspectForwardLightBindings(oxygen::ViewId { 1U }), nullptr);
  selection.local_lights.front().luminous_flux_lm = -1.0F;
  const auto failure = service.BuildLightGrid(inputs);
  ASSERT_FALSE(failure.has_value());
  EXPECT_EQ(failure.error().error,
    oxygen::vortex::LightingPreparationError::kInvalidInput);
  EXPECT_EQ(
    service.InspectForwardLightBindings(oxygen::ViewId { 1U }), nullptr);
  EXPECT_EQ(service.ResolveLightingFrameSlot(oxygen::ViewId { 1U }),
    kInvalidShaderVisibleIndex);
}

NOLINT_TEST_F(LightingServiceBehaviorTest,
  ShadowReferencesPublishOnlyForMatchingViewGeneration)
{
  using oxygen::vortex::ShadowFrameData;
  auto service = LightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 3U }, oxygen::frame::Slot { 0U });
  auto selection = FrameLightSelection {};
  selection.scene_generation = 7U;
  selection.selection_epoch = 11U;
  selection.local_lights = {
    FrameLocalLightSelection {
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
    },
  };
  const auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  const auto views = std::array {
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 17U },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr { &resolved_view },
      .composition_view = {},
    },
  };
  ASSERT_TRUE(service.BuildLightGrid(
    { .frame_light_set = &selection, .active_views = views }));
  const auto* initial
    = service.InspectForwardLightBindings(views.front().view_id);
  ASSERT_NE(initial, nullptr);
  const auto old_slot = service.ResolveLightingFrameSlot(views.front().view_id);
  auto shadow = ShadowFrameData {};
  shadow.bindings.frame_sequence = initial->frame_sequence;
  shadow.bindings.scene_generation = initial->scene_generation;
  shadow.bindings.selection_revision = initial->selection_revision;
  shadow.bindings.view_generation = initial->view_generation;
  shadow.bindings.view_status_srv = initial->build_status_srv;
  shadow.local_shadow_references.resize(1U);
  shadow.local_shadow_map_srv = oxygen::ShaderVisibleIndex { 101U };
  ASSERT_TRUE(service.PublishShadowReferences(views.front().view_id, shadow));
  const auto* complete
    = service.InspectForwardLightBindings(views.front().view_id);
  ASSERT_NE(complete, nullptr);
  EXPECT_EQ(complete->local_shadow_map_srv, shadow.local_shadow_map_srv);
  EXPECT_NE(service.ResolveLightingFrameSlot(views.front().view_id), old_slot);
  ++shadow.bindings.view_generation.at(0);
  const auto stale
    = service.PublishShadowReferences(views.front().view_id, shadow);
  ASSERT_FALSE(stale);
  EXPECT_EQ(stale.error().error,
    oxygen::vortex::LightingPreparationError::kGenerationMismatch);
  EXPECT_EQ(
    service.InspectForwardLightBindings(views.front().view_id), nullptr);
  EXPECT_FALSE(
    service.ResolveLightingFrameSlot(views.front().view_id).IsValid());
}

} // namespace
