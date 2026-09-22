//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
#include <Oxygen/Vortex/Shadows/Types/ProjectedLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::testing {
namespace {

  static_assert(!std::is_convertible_v<LightSelectionIndex, std::uint32_t>);
  static_assert(!std::is_convertible_v<ShadowRecordIndex, std::uint32_t>);
  static_assert(!std::is_assignable_v<ShadowRecordIndex&, LightSelectionIndex>);
  static_assert(!std::is_assignable_v<LightSelectionIndex&, ShadowRecordIndex>);
  static_assert(!std::is_assignable_v<ShadowCascadeIndex&, ShadowArrayLayer>);
  static_assert(!std::is_assignable_v<ShadowArrayLayer&, LightListOffset>);

  NOLINT_TEST(LightingIndexTest, AbsentReferencesUseTypedSentinels)
  {
    const auto reference = LightShadowReference {};
    EXPECT_EQ(reference.record_index, kInvalidShadowRecordIndex);
    EXPECT_EQ(reference.selection_index, kInvalidLightSelectionIndex);
    const auto directional = DirectionalShadowRecord {};
    EXPECT_EQ(directional.selection_index, kInvalidLightSelectionIndex);
    EXPECT_EQ(directional.first_cascade, kInvalidShadowCascadeIndex);
    EXPECT_NE(LightSelectionIndex { 0U }, kInvalidLightSelectionIndex);
    EXPECT_NE(ShadowRecordIndex { 0U }, kInvalidShadowRecordIndex);
  }

  NOLINT_TEST_F(LightingGpuAbiTest, CubeReferencesKeepDistinctSelectionIndices)
  {
    const auto records = std::array {
      LightShadowReference {},
      LightShadowReference {
        .projection_kind = kShadowProjectionLocalCube,
        .record_index = ShadowRecordIndex { 7U },
        .selection_index = LightSelectionIndex { 0x01000001U },
        .coverage_state = kShadowCoverageComplete,
      },
      LightShadowReference {
        .projection_kind = kShadowProjectionLocalCube,
        .record_index = ShadowRecordIndex { 11U },
        .selection_index = LightSelectionIndex { 0x80000003U },
        .coverage_state = kShadowCoverageComplete,
      },
      LightShadowReference {},
    };
    const auto expected = std::vector<std::uint32_t> {
      2U,
      7U,
      0x01000001U,
      2U,
      2U,
      11U,
      0x80000003U,
      2U,
      0U,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0U,
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 16U,
                .record_kind = 3U,
                .decoded_words = 4U,
                .first_element = 1U,
                .count = 3U,
              }),
      expected);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, DirectionalFamiliesDecodeSelectionAndCascadeRange)
  {
    const auto records = std::array {
      DirectionalShadowRecord {},
      DirectionalShadowRecord {
        .selection_index = LightSelectionIndex { 0x01000001U },
        .first_cascade = ShadowCascadeIndex { 5U },
        .cascade_count = 4U,
      },
      DirectionalShadowRecord {
        .selection_index = LightSelectionIndex { 0x80000003U },
        .first_cascade = ShadowCascadeIndex { 9U },
        .cascade_count = 2U,
      },
    };
    const auto expected = std::vector<std::uint32_t> {
      0x01000001U,
      5U,
      4U,
      0U,
      0x80000003U,
      9U,
      2U,
      0U,
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 16U,
                .record_kind = 4U,
                .decoded_words = 4U,
                .first_element = 1U,
                .count = 2U,
              }),
      expected);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, CascadesDecodeEveryLaneAndIntegerSurfaceIdentity)
  {
    const auto matrix = glm::mat4 {
      glm::vec4 { 1, 2, 3, 4 },
      glm::vec4 { 5, 6, 7, 8 },
      glm::vec4 { 9, 10, 11, 12 },
      glm::vec4 { 13, 14, 15, 16 },
    };
    const auto records = std::array {
      ShadowCascadeBinding {},
      ShadowCascadeBinding {
        .light_view_projection = matrix,
        .split_near = -17.0F,
        .split_far = 18.0F,
        .depth_bias = 0.125F,
        .normal_bias_m = 0.25F,
        .surface_srv = ShaderVisibleIndex { 0x80000001U },
        .array_layer = ShadowArrayLayer { 0x01000003U },
        .inverse_resolution = { 0.5F, 0.25F },
        .world_texel_size = 0.75F,
        .transition_width = 1.25F,
        .fade_begin = 16.0F,
        .fade_end = 18.0F,
      },
      ShadowCascadeBinding {
        .light_view_projection = matrix * 2.0F,
        .split_near = 19.0F,
        .split_far = 20.0F,
        .depth_bias = 0.0625F,
        .normal_bias_m = 0.5F,
        .inverse_resolution = { 0.125F, 0.0625F },
        .world_texel_size = 1.5F,
        .transition_width = 2.5F,
        .fade_begin = 19.5F,
        .fade_end = 20.0F,
      },
    };
    const auto word = [](const float value) -> std::uint32_t {
      return std::bit_cast<std::uint32_t>(value);
    };
    const auto expected = std::vector<std::uint32_t> {
      word(1),
      word(2),
      word(3),
      word(4),
      word(5),
      word(6),
      word(7),
      word(8),
      word(9),
      word(10),
      word(11),
      word(12),
      word(13),
      word(14),
      word(15),
      word(16),
      word(-17),
      word(18),
      word(0.125F),
      word(0.25F),
      0x80000001U,
      0x01000003U,
      0U,
      0U,
      word(0.5F),
      word(0.25F),
      word(0.75F),
      word(1.25F),
      word(16),
      word(18),
      0U,
      0U,
      word(2),
      word(4),
      word(6),
      word(8),
      word(10),
      word(12),
      word(14),
      word(16),
      word(18),
      word(20),
      word(22),
      word(24),
      word(26),
      word(28),
      word(30),
      word(32),
      word(19),
      word(20),
      word(0.0625F),
      word(0.5F),
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0U,
      0U,
      word(0.125F),
      word(0.0625F),
      word(1.5F),
      word(2.5F),
      word(19.5F),
      word(20),
      0U,
      0U,
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(records)),
                .stride = 128U,
                .record_kind = 13U,
                .decoded_words = 32U,
                .first_element = 1U,
                .count = 2U }),
      expected);
  }

  NOLINT_TEST_F(LightingGpuAbiTest,
    LocalProjectionRecordsDecodeAllFacesAndIntegerIdentities)
  {
    const auto matrix = glm::mat4 {
      glm::vec4 { 1, 2, 3, 4 },
      glm::vec4 { 5, 6, 7, 8 },
      glm::vec4 { 9, 10, 11, 12 },
      glm::vec4 { 13, 14, 15, 16 },
    };
    const auto projected = ProjectedLocalShadowRecord {
      .light_view_projection = matrix,
      .shadow_origin_ws = { -1.0F, 2.0F, 3.0F },
      .near_plane_m = 0.125F,
      .far_plane_m = 18.0F,
      .normal_bias_m = 0.25F,
      .depth_bias = 0.5F,
      .world_texel_size = 0.75F,
      .surface_srv = ShaderVisibleIndex { 0x80000001U },
      .array_layer = ShadowArrayLayer { 0x01000003U },
      .selection_index = LightSelectionIndex { 0x80000005U },
      .inverse_resolution = { 0.0625F, 0.03125F },
    };
    auto cube = CubeLocalShadowRecord {
      .shadow_origin_ws = projected.shadow_origin_ws,
      .near_plane_m = projected.near_plane_m,
      .far_plane_m = projected.far_plane_m,
      .normal_bias_m = projected.normal_bias_m,
      .depth_bias = projected.depth_bias,
      .world_texel_size = projected.world_texel_size,
      .surface_srv = projected.surface_srv,
      .first_array_layer = projected.array_layer,
      .selection_index = projected.selection_index,
      .inverse_resolution = projected.inverse_resolution,
    };
    for (std::size_t face = 0U; face < cube.face_light_view_projection.size();
      ++face) {
      cube.face_light_view_projection.at(face)
        = matrix * static_cast<float>(face + 1U);
    }
    auto projected_second = projected;
    projected_second.light_view_projection *= 8.0F;
    projected_second.surface_srv = kInvalidShaderVisibleIndex;
    projected_second.array_layer = kInvalidShadowArrayLayer;
    projected_second.selection_index = kInvalidLightSelectionIndex;
    auto cube_second = cube;
    for (auto& face : cube_second.face_light_view_projection) {
      face *= 8.0F;
    }
    cube_second.surface_srv = kInvalidShaderVisibleIndex;
    cube_second.first_array_layer = kInvalidShadowArrayLayer;
    cube_second.selection_index = kInvalidLightSelectionIndex;
    const auto projected_records = std::array {
      ProjectedLocalShadowRecord {},
      projected,
      projected_second,
    };
    const auto cube_records
      = std::array { CubeLocalShadowRecord {}, cube, cube_second };
    const auto word = [](const float value) -> std::uint32_t {
      return std::bit_cast<std::uint32_t>(value);
    };
    for (const bool cube_projection : { false, true }) {
      auto expected = std::vector<std::uint32_t> {};
      for (const bool second : { false, true }) {
        const auto face_count = cube_projection ? 6U : 1U;
        for (std::uint32_t face = 1U; face <= face_count; ++face) {
          for (std::uint32_t lane = 1U; lane <= 16U; ++lane) {
            expected.push_back(
              word(static_cast<float>(lane * face * (second ? 8U : 1U))));
          }
        }
        const auto tail = std::array {
          word(-1),
          word(2),
          word(3),
          word(0.125F),
          word(18),
          word(0.25F),
          word(0.5F),
          word(0.75F),
          second ? 0xFFFFFFFFU : 0x80000001U,
          second ? 0xFFFFFFFFU : 0x01000003U,
          second ? 0xFFFFFFFFU : 0x80000005U,
          0U,
          word(0.0625F),
          word(0.03125F),
          0U,
          0U,
        };
        expected.insert(expected.end(), tail.begin(), tail.end());
      }
      EXPECT_EQ(Decode({ .records = cube_projection
                    ? std::as_bytes(
                        std::span<const CubeLocalShadowRecord>(cube_records))
                    : std::as_bytes(std::span<const ProjectedLocalShadowRecord>(
                        projected_records)),
                  .stride = cube_projection ? 448U : 128U,
                  .record_kind = cube_projection ? 15U : 14U,
                  .decoded_words = cube_projection ? 112U : 32U,
                  .first_element = 1U,
                  .count = 2U }),
        expected);
    }
  }

} // namespace
} // namespace oxygen::vortex::testing
