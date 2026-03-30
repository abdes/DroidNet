//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

// Stable cache-manager-facing seam for the greenfield VSM stack.
// Pool snapshots carry shared GPU resource lifetime — this seam must be
// consumed within the current frame and not held across pool resets.
struct VsmCacheManagerSeam {
  VsmPhysicalPoolSnapshot physical_pool {};
  VsmHzbPoolSnapshot hzb_pool {};
  VsmVirtualAddressSpaceFrame current_frame {};
  VsmVirtualRemapTable previous_to_current_remap {};

  auto operator==(const VsmCacheManagerSeam&) const -> bool = default;
};

inline constexpr std::size_t kMaxPreferredPhysicalPoolSnapshotBytes = 128;

static_assert(std::is_copy_constructible_v<VsmPhysicalPoolSnapshot>);
static_assert(std::is_copy_assignable_v<VsmPhysicalPoolSnapshot>);
static_assert(
  sizeof(VsmPhysicalPoolSnapshot) <= kMaxPreferredPhysicalPoolSnapshotBytes);
static_assert(std::is_copy_constructible_v<VsmHzbPoolSnapshot>);
static_assert(std::is_copy_constructible_v<VsmVirtualAddressSpaceFrame>);
static_assert(std::is_copy_constructible_v<VsmVirtualRemapTable>);
static_assert(std::is_copy_constructible_v<VsmCacheManagerSeam>);

} // namespace oxygen::renderer::vsm
