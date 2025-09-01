//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Defines visibility and memory access properties of descriptor heaps/pools.
/*!
 Descriptor heaps or pools in their entirety are either shader-visible or
 CPU-only. Individual descriptors inherit this property from the
 heap/pool they belong to.

 In D3D12, this maps directly to whether a descriptor heap is shader-visible
 or not. In Vulkan, this affects descriptor set allocation strategies and
 whether descriptors are host-visible or device-local.

 The primary use of CPU-only heaps is for staging descriptors before
 copying them to shader-visible heaps, which can be more efficient for certain
 update patterns. It also supports persistent/immutable descriptor heaps.
*/
enum class DescriptorVisibility : std::uint8_t {
  kNone, //!< No visibility, invalid state.

  kShaderVisible, //!< GPU-accessible descriptor heap/pool.
  kCpuOnly, //!< CPU-only descriptor heap/pool, not directly accessible to
            //!< shaders.

  kMaxDescriptorVisibility //!< Sentinel value for the number of visibilities.
};

//! Check if the given resource view type is valid.
constexpr auto IsValid(DescriptorVisibility visibility) noexcept
{
  return visibility > DescriptorVisibility::kNone
    && visibility < DescriptorVisibility::kMaxDescriptorVisibility;
}

//! Check if the given resource view type is undefined.
constexpr auto IsUndefined(DescriptorVisibility visibility) noexcept
{
  return visibility == DescriptorVisibility::kNone;
}

//! String representation of enum values in `DescriptorVisibility`.
OXGN_GFX_API auto to_string(DescriptorVisibility value) -> const char*;

} // namespace oxygen::graphics
