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

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Types/ClusterLightRange.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridPassConstants.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::testing {
namespace {

  NOLINT_TEST_F(
    LightingGpuAbiTest, ClusterRangesDecodeSentinelAndAdjacentRecord)
  {
    const auto records = std::array {
      ClusterLightRange { .offset = LightListOffset { 99U }, .count = 77U },
      ClusterLightRange { .offset = kCompleteLightListOffset, .count = 4096U },
      ClusterLightRange {
        .offset = LightListOffset { 0x01000001U },
        .count = 33U,
      },
    };
    const auto expected = std::vector<std::uint32_t> {
      0xFFFFFFFFU,
      4096U,
      0x01000001U,
      33U,
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 8U,
                .record_kind = 0U,
                .decoded_words = 2U,
                .first_element = 1U,
                .count = 2U,
              }),
      expected);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, BuildStatusDecodesHighWordsWithoutFloatConversion)
  {
    const auto records = std::array {
      LightGridBuildStatus {},
      LightGridBuildStatus {
        .state = kLightGridBuildValid,
        .reason = kLightGridReasonNone,
        .written_index_count = 0x01000003U,
        .fallback_cell_count = 0x80000005U,
        .required_index_count = { 0xFFFFFFFDU, 0x12345678U },
        .selection_revision = { 0x87654321U, 0xFEDCBA98U },
      },
      LightGridBuildStatus {
        .state = kLightGridBuildFailed,
        .reason = kLightGridReasonGenerationMismatch,
        .written_index_count = 31U,
        .fallback_cell_count = 64U,
        .required_index_count = { 0U, 1U },
        .selection_revision = { 0x11111111U, 0x99999999U },
      },
    };
    const auto expected = std::vector<std::uint32_t> {
      1U,
      0U,
      0x01000003U,
      0x80000005U,
      0xFFFFFFFDU,
      0x12345678U,
      0x87654321U,
      0xFEDCBA98U,
      2U,
      4U,
      31U,
      64U,
      0U,
      1U,
      0x11111111U,
      0x99999999U,
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 32U,
                .record_kind = 1U,
                .decoded_words = 8U,
                .first_element = 1U,
                .count = 2U,
              }),
      expected);
  }

  NOLINT_TEST_F(LightingGpuAbiTest, GridDispatchDecodesDescriptorsAndOffset)
  {
    const auto records = std::array {
      LightGridPassConstants {},
      LightGridPassConstants {
        .lighting_bindings_srv = ShaderVisibleIndex { 0x01000001U },
        .ranges_uav = ShaderVisibleIndex { 0x80000002U },
        .indices_uav = kInvalidShaderVisibleIndex,
        .status_uav = ShaderVisibleIndex { 0x10000004U },
        .counts_uav = ShaderVisibleIndex { 0x20000005U },
        .offsets_uav = ShaderVisibleIndex { 0x40000006U },
        .subpass = 2U,
        .work_count = 0xABCD0123U,
        .work_offset = { 0xFFFFFFFEU, 3U },
        .scan_stride = 16U,
        .scan_phase = 1U,
      },
      LightGridPassConstants {},
    };
    const auto expected = std::vector<std::uint32_t> {
      0x01000001U,
      0x80000002U,
      0xFFFFFFFFU,
      0x10000004U,
      0x20000005U,
      0x40000006U,
      2U,
      0xABCD0123U,
      0xFFFFFFFEU,
      3U,
      16U,
      1U,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
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
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 48U,
                .record_kind = 2U,
                .decoded_words = 12U,
                .first_element = 1U,
                .count = 2U,
              }),
      expected);
  }

  NOLINT_TEST_F(LightingGpuAbiTest, DetectsChangedUploadLane)
  {
    auto records = std::array {
      ClusterLightRange {
        .offset = LightListOffset { 17U },
        .count = 33U,
      },
    };
    const auto expected = std::vector<std::uint32_t> { 17U, 33U };
    ASSERT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 8U,
                .record_kind = 0U,
                .decoded_words = 2U,
                .first_element = 0U,
                .count = 1U,
              }),
      expected);
    records.at(0).count = 32U;
    EXPECT_NE(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 8U,
                .record_kind = 0U,
                .decoded_words = 2U,
                .first_element = 0U,
                .count = 1U,
              }),
      expected);
  }

  NOLINT_TEST_F(LightingGpuAbiTest, MetadataPreservesRectangleAndSignedDepth)
  {
    const auto records = std::array {
      LightGridMetadata {},
      LightGridMetadata {
        .grid_size = { 30U, 17U, 32U },
        .pixel_size_shift = 6U,
        .content_origin_px = { 13.5F, 7.25F },
        .content_extent_px = { 1919.0F, 1079.0F },
        .grid_z_params = { 0.5F, 0.25F, 4.0F },
        .far_depth_m = 1024.0F,
        .near_depth_m = 1.0F,
        .projection_kind = kLightGridPerspective,
      },
      LightGridMetadata {
        .grid_size = { 1U, 2U, 32U },
        .pixel_size_shift = 6U,
        .content_extent_px = { 63.0F, 65.0F },
        .far_depth_m = 24.0F,
        .near_depth_m = -8.0F,
        .projection_kind = kLightGridOrthographic,
      },
    };
    const auto bits = [](float value) -> std::uint32_t {
      return std::bit_cast<std::uint32_t>(value);
    };
    const auto expected = std::vector<std::uint32_t> {
      30U,
      17U,
      32U,
      6U,
      bits(13.5F),
      bits(7.25F),
      bits(1919.0F),
      bits(1079.0F),
      bits(0.5F),
      bits(0.25F),
      bits(4.0F),
      bits(1024.0F),
      bits(1.0F),
      0U,
      0U,
      0U,
      1U,
      2U,
      32U,
      6U,
      0U,
      0U,
      bits(63.0F),
      bits(65.0F),
      0U,
      0U,
      0U,
      bits(24.0F),
      bits(-8.0F),
      1U,
      0U,
      0U,
    };
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = 64U,
                .record_kind = 5U,
                .decoded_words = 16U,
                .first_element = 1U,
                .count = 2U,
              }),
      expected);
  }

} // namespace
} // namespace oxygen::vortex::testing
