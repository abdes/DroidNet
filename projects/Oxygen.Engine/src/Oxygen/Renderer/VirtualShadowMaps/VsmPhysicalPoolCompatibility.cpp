//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPoolCompatibility.h>

namespace oxygen::renderer::vsm {

auto ComputePhysicalPoolCompatibility(const VsmPhysicalPoolConfig& current,
  const VsmPhysicalPoolConfig& requested) noexcept
  -> VsmPhysicalPoolCompatibilityResult
{
  if (!IsValid(current)) {
    return VsmPhysicalPoolCompatibilityResult::kInvalidCurrentConfig;
  }
  if (!IsValid(requested)) {
    return VsmPhysicalPoolCompatibilityResult::kInvalidRequestedConfig;
  }
  if (current.page_size_texels != requested.page_size_texels) {
    return VsmPhysicalPoolCompatibilityResult::kPageSizeMismatch;
  }
  if (current.array_slice_count != requested.array_slice_count) {
    return VsmPhysicalPoolCompatibilityResult::kSliceCountMismatch;
  }
  if (current.physical_tile_capacity != requested.physical_tile_capacity) {
    return VsmPhysicalPoolCompatibilityResult::kTileCapacityMismatch;
  }
  if (current.depth_format != requested.depth_format) {
    return VsmPhysicalPoolCompatibilityResult::kDepthFormatMismatch;
  }
  if (current.slice_roles != requested.slice_roles) {
    return VsmPhysicalPoolCompatibilityResult::kSliceRolesMismatch;
  }

  return VsmPhysicalPoolCompatibilityResult::kCompatible;
}

} // namespace oxygen::renderer::vsm
