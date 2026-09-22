//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
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

} // namespace
} // namespace oxygen::vortex::testing
