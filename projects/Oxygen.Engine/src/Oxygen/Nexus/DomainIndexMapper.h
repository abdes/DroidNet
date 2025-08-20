//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Nexus/Types/Domain.h>
#include <Oxygen/Nexus/api_export.h>

namespace oxygen::graphics {
class DescriptorAllocator;
}

namespace oxygen::nexus {

namespace detail {
  //! Opaque implementation type for PIMPL pattern.
  struct DomainIndexMapperImpl;
} // namespace detail

//! Maps bindless domain keys to absolute descriptor heap ranges.
/*!
 Provides bidirectional mapping between domain keys (resource type + visibility)
 and their corresponding absolute ranges in the global bindless descriptor heap.
 Captures allocator state for specified domains at construction time.

 ### Key Features

 - **Domain-to-Range Mapping**: Resolves domain keys to absolute heap ranges
 - **Reverse Lookup**: Finds domain key from absolute handle index
 - **Selective Capture**: Only captures state for explicitly specified domains
 - **Zero-Capacity Support**: Handles domains with no current allocations
 - **PIMPL Design**: Implementation details hidden for ABI stability

 ### Usage Patterns

 ```cpp
 using namespace oxygen::graphics;

 // Initialize with allocator and known domains
 DomainIndexMapper mapper(allocator, {
   {ResourceViewType::Texture2D, DescriptorVisibility::Pixel},
   {ResourceViewType::Buffer, DescriptorVisibility::Vertex}
 });

 // Get range for a domain
 auto key = DomainKey{ResourceViewType::Texture2D,
                      DescriptorVisibility::Pixel};
 if (auto range = mapper.GetDomainRange(key)) {
   // Use range.start and range.capacity
 }

 // Reverse lookup from absolute index
 auto handle = oxygen::bindless::Handle{42};
 if (auto domain = mapper.ResolveDomain(handle)) {
   // Found the domain containing this handle
 }
 ```

 ### Architecture Notes

 The mapper captures allocator state for explicitly specified domains at
 construction time and remains immutable thereafter. It provides efficient
 translation between logical domains and their fixed physical heap positions
 for the bindless rendering pipeline.

 @warning Mapper lifetime must not exceed the associated DescriptorAllocator.
 @see DomainKey, DomainRange, DescriptorAllocator
*/
class DomainIndexMapper {
public:
  //! Construct mapper with allocator reference and initial domains.
  OXGN_NXS_API explicit DomainIndexMapper(
    const oxygen::graphics::DescriptorAllocator& allocator,
    std::initializer_list<DomainKey> domains = {});
  //! Default constructor deleted - requires allocator reference.
  DomainIndexMapper() = delete;

  //! Destructor for PIMPL cleanup.
  OXGN_NXS_API ~DomainIndexMapper();

  OXYGEN_MAKE_NON_COPYABLE(DomainIndexMapper)
  OXYGEN_DEFAULT_MOVABLE(DomainIndexMapper)

  //! Get absolute descriptor range for a domain key.
  OXGN_NXS_NDAPI auto GetDomainRange(DomainKey const& k) const noexcept
    -> std::optional<DomainRange>;

  //! Resolve domain key from absolute bindless handle.
  OXGN_NXS_NDAPI auto ResolveDomain(
    oxygen::bindless::Handle index) const noexcept -> std::optional<DomainKey>;

private:
  std::unique_ptr<detail::DomainIndexMapperImpl> pimpl_;
};

} // namespace oxygen::nexus
