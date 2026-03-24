//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPoolCompatibility.h>

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::ComputePhysicalPoolCompatibility;
using oxygen::renderer::vsm::to_string;
using oxygen::renderer::vsm::VsmPhysicalPoolCompatibilityResult;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;

auto MakeShadowConfig() -> VsmPhysicalPoolConfig
{
  return {
    .page_size_texels = 128,
    .physical_tile_capacity = 512,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
      VsmPhysicalPoolSliceRole::kStaticDepth },
    .debug_name = "compatibility-test",
  };
}

NOLINT_TEST(VirtualShadowContractsScaffoldTest,
  PhysicalPoolCompatibilityReportsExactMismatchReasons)
{
  const auto base = MakeShadowConfig();
  EXPECT_EQ(ComputePhysicalPoolCompatibility(base, base),
    VsmPhysicalPoolCompatibilityResult::kCompatible);

  auto page_size_mismatch = base;
  page_size_mismatch.page_size_texels = 64;
  EXPECT_EQ(ComputePhysicalPoolCompatibility(base, page_size_mismatch),
    VsmPhysicalPoolCompatibilityResult::kPageSizeMismatch);

  auto format_mismatch = base;
  format_mismatch.depth_format = Format::kDepth32Stencil8;
  EXPECT_EQ(ComputePhysicalPoolCompatibility(base, format_mismatch),
    VsmPhysicalPoolCompatibilityResult::kDepthFormatMismatch);

  auto slice_count_mismatch = base;
  slice_count_mismatch.physical_tile_capacity = 256;
  slice_count_mismatch.array_slice_count = 1;
  slice_count_mismatch.slice_roles
    = { VsmPhysicalPoolSliceRole::kDynamicDepth };
  EXPECT_EQ(ComputePhysicalPoolCompatibility(base, slice_count_mismatch),
    VsmPhysicalPoolCompatibilityResult::kSliceCountMismatch);

  auto slice_roles_mismatch = base;
  slice_roles_mismatch.slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
    VsmPhysicalPoolSliceRole::kDynamicDepth };
  EXPECT_EQ(ComputePhysicalPoolCompatibility(base, slice_roles_mismatch),
    VsmPhysicalPoolCompatibilityResult::kInvalidRequestedConfig);

  auto non_square_layout = base;
  non_square_layout.physical_tile_capacity = 513;
  EXPECT_EQ(ComputePhysicalPoolCompatibility(base, non_square_layout),
    VsmPhysicalPoolCompatibilityResult::kInvalidRequestedConfig);

  EXPECT_STREQ(
    to_string(VsmPhysicalPoolCompatibilityResult::kSliceCountMismatch),
    "SliceCountMismatch");
}

} // namespace
