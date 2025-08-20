//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Nexus/api_export.h>

namespace oxygen::nexus {

//! Key identifying a bindless descriptor domain.
/*!
 Uniquely identifies a bindless domain by resource view type and visibility.
 Used as the key for mapping to absolute descriptor ranges in the global
 bindless heap.

 @see DomainRange, DomainIndexMapper
*/
struct DomainKey {
  //! Resource view type for this domain (e.g., Texture2D, Buffer).
  oxygen::graphics::ResourceViewType view_type {};

  //! Shader stage visibility for this domain (e.g., Vertex, Pixel).
  oxygen::graphics::DescriptorVisibility visibility {};

  //! Equality comparison for domain keys.
  bool operator==(DomainKey const& o) const noexcept
  {
    return view_type == o.view_type && visibility == o.visibility;
  }
};

//! Absolute range within the global bindless descriptor heap.
/*!
 Represents a contiguous range of bindless descriptor slots allocated for a
 specific domain. The range is defined by a starting handle and capacity.

 @see DomainKey, DomainIndexMapper::GetDomainRange
*/
struct DomainRange {
  //! Starting handle index in the global bindless heap.
  oxygen::bindless::Handle start {};

  //! Number of descriptor slots allocated for this domain.
  oxygen::bindless::Capacity capacity {};
};

//! Hash function for DomainKey to enable use in unordered containers.
/*!
 Combines resource view type and visibility into a single hash value using
 bit shifting and XOR operations for good distribution.

 @see DomainKey
*/
struct DomainKeyHash {
  //! Compute hash value for a domain key.
  std::size_t operator()(DomainKey const& k) const noexcept
  {
    return (static_cast<std::size_t>(k.view_type) << 16)
      ^ static_cast<std::size_t>(k.visibility);
  }
};

} // namespace oxygen::nexus
