//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Types/DeferredLightConstants.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

namespace oxygen::vortex::testing {
namespace {

  constexpr auto Word(const float value) -> std::uint32_t
  {
    return std::bit_cast<std::uint32_t>(value);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, LocalEvaluationDecodesEveryLaneAndIntegerFlags)
  {
    const auto records = std::array {
      ForwardLocalLightRecord {},
      ForwardLocalLightRecord {
        .position_ws = { 1.0F, 2.0F, 3.0F },
        .range_m = 4.0F,
        .intensity_rgb_cd = { 5.0F, 6.0F, 7.0F },
        .source_radius_m = 8.0F,
        .emitted_direction_ws = { 9.0F, 10.0F, 11.0F },
        .inverse_range_m = 12.0F,
        .outer_cone_cosine = 0.125F,
        .inverse_cone_cosine_width = 0.5F,
        .kind = 0U,
        .flags = 0x80000001U,
        .selection_index = LightSelectionIndex { 0x01000001U },
      },
      ForwardLocalLightRecord {
        .position_ws = { -1.0F, -2.0F, -3.0F },
        .range_m = 16.0F,
        .intensity_rgb_cd = { 0.0F, 32.0F, 64.0F },
        .source_radius_m = 0.0F,
        .emitted_direction_ws = { 0.0F, -1.0F, 0.0F },
        .inverse_range_m = 0.0625F,
        .outer_cone_cosine = 0.25F,
        .inverse_cone_cosine_width = 0.375F,
        .kind = 1U,
        .flags = 0x01000003U,
        .selection_index = kInvalidLightSelectionIndex,
      },
    };
    const auto expected = std::vector<std::uint32_t> {
      Word(1),
      Word(2),
      Word(3),
      Word(4),
      Word(5),
      Word(6),
      Word(7),
      Word(8),
      Word(9),
      Word(10),
      Word(11),
      Word(12),
      Word(0.125F),
      Word(0.5F),
      0U,
      0x80000001U,
      0x01000001U,
      0U,
      0U,
      0U,
      Word(-1),
      Word(-2),
      Word(-3),
      Word(16),
      Word(0),
      Word(32),
      Word(64),
      Word(0),
      Word(0),
      Word(-1),
      Word(0),
      Word(0.0625F),
      Word(0.25F),
      Word(0.375F),
      1U,
      0x01000003U,
      0xFFFFFFFFU,
      0U,
      0U,
      0U,
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(records)),
                .stride = 80U,
                .record_kind = 8U,
                .decoded_words = 20U,
                .first_element = 1U,
                .count = 2U }),
      expected);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, DirectionalEvaluationPreservesSlotsAndReservedZeros)
  {
    const auto records = std::array {
      DirectionalLightForwardData {},
      DirectionalLightForwardData {
        .direction_to_source_ws = { 1.0F, 2.0F, 3.0F },
        .atmosphere_light_slot = AtmosphereLightIndex { 1U },
        .illuminance_rgb_lux = { 4.0F, 8.0F, 16.0F },
        .flags = 0x80000003U,
        .ground_transmittance_rgb = { 0.125F, 0.25F, 0.5F },
        .atmosphere_mode_flags = 0x01000007U,
        .selection_index = LightSelectionIndex { 0x01000001U },
      },
      DirectionalLightForwardData {
        .direction_to_source_ws = { -1.0F, -2.0F, -3.0F },
        .atmosphere_light_slot = kInvalidAtmosphereLightIndex,
        .illuminance_rgb_lux = { 32.0F, 64.0F, 128.0F },
        .flags = 2U,
        .ground_transmittance_rgb = { 1.0F, 0.75F, 0.0F },
        .atmosphere_mode_flags = 0U,
        .selection_index = LightSelectionIndex { 0x80000001U },
      },
    };
    const auto expected = std::vector<std::uint32_t> {
      Word(1),
      Word(2),
      Word(3),
      1U,
      Word(4),
      Word(8),
      Word(16),
      0x80000003U,
      Word(0.125F),
      Word(0.25F),
      Word(0.5F),
      0x01000007U,
      0x01000001U,
      0U,
      0U,
      0U,
      Word(-1),
      Word(-2),
      Word(-3),
      0xFFFFFFFFU,
      Word(32),
      Word(64),
      Word(128),
      2U,
      Word(1),
      Word(0.75F),
      Word(0),
      0U,
      0x80000001U,
      0U,
      0U,
      0U,
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(records)),
                .stride = 64U,
                .record_kind = 9U,
                .decoded_words = 16U,
                .first_element = 1U,
                .count = 2U }),
      expected);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, LightingHeaderDecodesAllDescriptorsAndFullGenerations)
  {
    const auto records = std::array {
      LightingFrameBindings {},
      LightingFrameBindings {
        .directional_records_srv = ShaderVisibleIndex { 0x01000001U },
        .local_records_srv = ShaderVisibleIndex { 0x80000002U },
        .cluster_ranges_srv = ShaderVisibleIndex { 3U },
        .local_indices_srv = kInvalidShaderVisibleIndex,
        .directional_count = 5U,
        .local_count = 6U,
        .cluster_count = 7U,
        .index_capacity = 8U,
        .directional_shadow_map_srv = ShaderVisibleIndex { 9U },
        .local_shadow_map_srv = ShaderVisibleIndex { 10U },
        .build_status_srv = ShaderVisibleIndex { 11U },
        .grid_metadata_srv = ShaderVisibleIndex { 12U },
        .scene_generation = { 0x11111111U, 0xEEEEEEEEU },
        .selection_revision = { 0x22222222U, 0xDDDDDDDDU },
        .frame_sequence = { 0x33333333U, 0xCCCCCCCCU },
        .view_generation = { 0x44444444U, 0xBBBBBBBBU },
        .publication_state = kLightingPublicationRecorded,
        .brdf_energy_srv = ShaderVisibleIndex { 22U },
        .brdf_model_revision = 2U,
      },
      LightingFrameBindings {},
    };
    const auto expected = std::vector<std::uint32_t> {
      0x01000001U,
      0x80000002U,
      3U,
      0xFFFFFFFFU,
      5U,
      6U,
      7U,
      8U,
      9U,
      10U,
      11U,
      12U,
      0x11111111U,
      0xEEEEEEEEU,
      0x22222222U,
      0xDDDDDDDDU,
      0x33333333U,
      0xCCCCCCCCU,
      0x44444444U,
      0xBBBBBBBBU,
      2U,
      22U,
      2U,
      0U,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0U,
      0U,
      0U,
      0U,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0U,
      0U,
      0U,
      0U,
      0U,
      0U,
      0U,
      0U,
      0U,
      0xFFFFFFFFU,
      0U,
      0U,
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(records)),
                .stride = 96U,
                .record_kind = 10U,
                .decoded_words = 24U,
                .first_element = 1U,
                .count = 2U }),
      expected);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, DeferredDrawDecodesNonsymmetricMatrixAndTypedIndices)
  {
    const auto matrix = glm::mat4 {
      glm::vec4 { 1, 2, 3, 4 },
      glm::vec4 { 5, 6, 7, 8 },
      glm::vec4 { 9, 10, 11, 12 },
      glm::vec4 { 13, 14, 15, 16 },
    };
    const auto records = std::array {
      DeferredLightConstants {},
      DeferredLightConstants {
        .light_world_matrix = matrix,
        .light_type = 1U,
        .selection_index = LightSelectionIndex { 0x01000001U },
        .light_geometry_vertices_srv = kInvalidShaderVisibleIndex,
        .light_geometry_vertex_count = 0x80000003U,
      },
      DeferredLightConstants {
        .light_world_matrix = matrix * 2.0F,
        .light_type = 3U,
        .selection_index = kInvalidLightSelectionIndex,
        .light_geometry_vertices_srv = ShaderVisibleIndex { 0x80000002U },
        .light_geometry_vertex_count = 0U,
      },
    };
    const auto expected = std::vector<std::uint32_t> {
      Word(151),
      Word(166),
      Word(181),
      Word(196),
      1U,
      0x01000001U,
      0xFFFFFFFFU,
      0x80000003U,
      Word(302),
      Word(332),
      Word(362),
      Word(392),
      3U,
      0xFFFFFFFFU,
      0x80000002U,
      0U,
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(records)),
                .stride = 80U,
                .record_kind = 11U,
                .decoded_words = 8U,
                .first_element = 1U,
                .count = 2U,
                .constant_buffer_records = true }),
      expected);
  }

  NOLINT_TEST_F(LightingGpuAbiTest, ViewRootPreservesExpectedLightingGeneration)
  {
    const auto records = std::array {
      ViewFrameBindings {},
      ViewFrameBindings {
        .draw_frame_slot = ShaderVisibleIndex { 1U },
        .lighting_frame_slot = ShaderVisibleIndex { 0x01000001U },
        .environment_frame_slot = ShaderVisibleIndex { 3U },
        .frame_exposure_slot = ShaderVisibleIndex { 4U },
        .scene_texture_frame_slot = ShaderVisibleIndex { 5U },
        .scene_depth_slot = ShaderVisibleIndex { 6U },
        .screen_hzb_frame_slot = ShaderVisibleIndex { 7U },
        .shadow_frame_slot = ShaderVisibleIndex { 8U },
        .virtual_shadow_frame_slot = ShaderVisibleIndex { 9U },
        .post_process_frame_slot = ShaderVisibleIndex { 10U },
        .debug_frame_slot = ShaderVisibleIndex { 11U },
        .history_frame_slot = ShaderVisibleIndex { 12U },
        .ray_tracing_frame_slot = ShaderVisibleIndex { 13U },
        .exposure_status_uav = ShaderVisibleIndex { 14U },
        .lighting_view_generation = { 0xFFFFFFFEU, 0x87654321U },
      },
      ViewFrameBindings {},
    };
    const auto expected = std::vector<std::uint32_t> {
      1U,
      0x01000001U,
      3U,
      4U,
      5U,
      6U,
      7U,
      8U,
      9U,
      10U,
      11U,
      12U,
      13U,
      14U,
      0xFFFFFFFEU,
      0x87654321U,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0U,
      0U,
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(records)),
                .stride = 64U,
                .record_kind = 12U,
                .decoded_words = 16U,
                .first_element = 1U,
                .count = 2U }),
      expected);
  }

} // namespace
} // namespace oxygen::vortex::testing
