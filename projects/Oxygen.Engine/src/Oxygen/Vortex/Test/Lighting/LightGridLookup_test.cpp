//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/ext/vector_float2.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Types/ClusterLightRange.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::testing {
namespace {

  struct alignas(16) GridLookupProbeInput {
    LightGridMetadata grid;
    glm::vec2 screen_position { 0.0F };
    float view_depth { 0.0F };
    std::uint32_t reserved { 0U };
  };

  struct alignas(16) LightIterationProbeInput {
    ClusterLightRange range;
    std::uint32_t local_count { 0U };
    std::uint32_t reserved { 0U };
  };

  // Test-input transport mirrors the probe declarations, not the lookup math.
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(GridLookupProbeInput) == 80U);
  static_assert(offsetof(GridLookupProbeInput, screen_position) == 64U);
  static_assert(offsetof(GridLookupProbeInput, view_depth) == 72U);
  static_assert(offsetof(GridLookupProbeInput, reserved) == 76U);
  static_assert(sizeof(LightIterationProbeInput) == 16U);
  static_assert(offsetof(LightIterationProbeInput, local_count) == 8U);
  static_assert(offsetof(LightIterationProbeInput, reserved) == 12U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(LightingGpuAbiTest, LookupSubtractsOriginAndKeepsSignedDepth)
  {
    const auto grid = LightGridMetadata {
      .grid_size = { 2U, 1U, 4U },
      .pixel_size_shift = 6U,
      .content_origin_px = { 13.5F, 7.5F },
      .content_extent_px = { 65.0F, 63.0F },
      .far_depth_m = 8.0F,
      .near_depth_m = -8.0F,
      .projection_kind = kLightGridOrthographic,
    };
    const auto cases = std::array {
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 13.5F, 7.5F },
        .view_depth = -8.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 76.5F, 7.5F },
        .view_depth = -8.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 77.5F, 7.5F },
        .view_depth = -8.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 12.5F, 7.5F },
        .view_depth = -8.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 100.5F, 90.5F },
        .view_depth = -8.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 13.5F, 7.5F },
        .view_depth = -4.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 13.5F, 7.5F },
        .view_depth = 0.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 13.5F, 7.5F },
        .view_depth = 4.0F,
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 77.5F, 7.5F },
        .view_depth = 8.0F,
      },
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(cases)),
                .stride = 80U,
                .record_kind = 6U,
                .decoded_words = 1U,
                .count = static_cast<std::uint32_t>(cases.size()),
              }),
      (std::vector<std::uint32_t> { 0U, 0U, 1U, 0U, 1U, 2U, 4U, 6U, 7U }));
  }

  NOLINT_TEST_F(LightingGpuAbiTest, LookupKeepsFractionalFinalTiles)
  {
    // A 64.75-pixel extent contains a second tile only 0.75 pixels wide.
    // Raster sample centers in that tile must not be clamped into tile zero.
    const auto grid = LightGridMetadata {
      .grid_size = { 2U, 2U, 1U },
      .pixel_size_shift = 6U,
      .content_origin_px = { 13.25F, 7.25F },
      .content_extent_px = { 64.75F, 64.75F },
      .far_depth_m = 1.0F,
      .near_depth_m = 0.0F,
      .projection_kind = kLightGridOrthographic,
    };
    const auto cases = std::array {
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 76.5F, 70.5F },
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 77.5F, 70.5F },
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 76.5F, 71.5F },
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 77.5F, 71.5F },
      },
      GridLookupProbeInput {
        .grid = grid,
        .screen_position = { 78.5F, 72.5F },
      },
      GridLookupProbeInput { .grid = grid, .screen_position = { 12.5F, 6.5F } },
    };
    EXPECT_EQ(Decode({ .records = std::as_bytes(std::span(cases)),
                .stride = 80U,
                .record_kind = 6U,
                .decoded_words = 1U,
                .count = static_cast<std::uint32_t>(cases.size()) }),
      (std::vector<std::uint32_t> { 0U, 1U, 2U, 3U, 3U, 0U }));
  }

  NOLINT_TEST_F(LightingGpuAbiTest, LookupUsesPerspectiveLogSlicesAtBoundaries)
  {
    const auto grid = LightGridMetadata {
      .grid_size = { 1U, 1U, 4U },
      .pixel_size_shift = 6U,
      .content_extent_px = { 64.0F, 64.0F },
      .grid_z_params = { 1.0F, 0.0F, 1.0F },
      .far_depth_m = 8.0F,
      .near_depth_m = 1.0F,
      .projection_kind = kLightGridPerspective,
    };
    const auto cases = std::array {
      GridLookupProbeInput { .grid = grid, .view_depth = 1.0F },
      GridLookupProbeInput { .grid = grid, .view_depth = 2.0F },
      GridLookupProbeInput { .grid = grid, .view_depth = 4.0F },
      GridLookupProbeInput { .grid = grid, .view_depth = 8.0F },
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(cases)),
                .stride = 80U,
                .record_kind = 6U,
                .decoded_words = 1U,
                .count = 4U,
              }),
      (std::vector<std::uint32_t> { 0U, 1U, 2U, 3U }));
  }

  NOLINT_TEST_F(LightingGpuAbiTest, CompleteAndEmptyRangesNeedNoIndexDescriptor)
  {
    const auto cases = std::array {
      LightIterationProbeInput { .range = {}, .local_count = 33U },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 1U },
        .local_count = 1U,
      },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 31U },
        .local_count = 31U,
      },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 32U },
        .local_count = 32U,
      },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 33U },
        .local_count = 33U,
      },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 4096U },
        .local_count = 4096U,
      },
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(cases)),
                .stride = 16U,
                .record_kind = 7U,
                .decoded_words = 5U,
                .count = 6U,
              }),
      (std::vector<std::uint32_t> {
        1U,
        0U,
        0U,
        0U,
        0U,
        1U,
        1U,
        0U,
        1U,
        0U,
        1U,
        31U,
        465U,
        0x7FFFFFFFU,
        0U,
        1U,
        32U,
        496U,
        0xFFFFFFFFU,
        0U,
        1U,
        33U,
        528U,
        0xFFFFFFFFU,
        1U,
        1U,
        4096U,
        8386560U,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
      }));
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, CompactAndCompleteCellsUseOneIterationContract)
  {
    const auto indices = std::array { 5U, 1U, 4U, 0U, 2U, 3U };
    const auto slot = PublishIndices(indices);
    const auto cases = std::array {
      LightIterationProbeInput {
        .range = { .offset = LightListOffset { 1U }, .count = 3U },
        .local_count = 6U,
      },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 6U },
        .local_count = 6U,
      },
      LightIterationProbeInput { .range = {}, .local_count = 6U },
      LightIterationProbeInput {
        .range = { .offset = LightListOffset { 5U }, .count = 2U },
        .local_count = 6U,
      },
      LightIterationProbeInput {
        .range = { .offset = kCompleteLightListOffset, .count = 5U },
        .local_count = 6U,
      },
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(cases)),
                .stride = 16U,
                .record_kind = 7U,
                .decoded_words = 5U,
                .count = 5U,
                .indices_srv = slot,
              }),
      (std::vector<std::uint32_t> {
        1U,
        3U,
        5U,
        19U,
        0U,
        1U,
        6U,
        15U,
        63U,
        0U,
        1U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
      }));
  }

} // namespace
} // namespace oxygen::vortex::testing
