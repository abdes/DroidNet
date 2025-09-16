//===----------------------------------------------------------------------===//
// DomainIndexMapper implementation
//===----------------------------------------------------------------------===//

#include <unordered_map>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Nexus/DomainIndexMapper.h>

using oxygen::nexus::DomainIndexMapper;
using oxygen::nexus::DomainKey;
using oxygen::nexus::DomainRange;

namespace oxygen::nexus::detail {
struct DomainIndexMapperImpl {
  std::unordered_map<DomainKey, DomainRange, DomainKeyHash> map;
};
} // namespace oxygen::nexus::detail

using oxygen::nexus::detail::DomainIndexMapperImpl;

/*!
 Creates a domain index mapper that captures the current state of the provided
 DescriptorAllocator for the explicitly specified domains only. The mapper
 remains immutable after construction and only knows about the domains provided
 in the initializer list.

 @param allocator DescriptorAllocator to query for domain ranges
 @param domains Specific domain keys to capture and register

### Performance Characteristics

- Time Complexity: O(d) where d is number of specified domains
- Memory: O(d) for domain registration
- Optimization: Only specified domains are queried and cached

### Usage Examples

 ```cpp
 DomainIndexMapper mapper(allocator, {
   {ResourceViewType::Texture2D, DescriptorVisibility::Pixel},
   {ResourceViewType::Buffer, DescriptorVisibility::All}
 });
 ```

 @note Only specified domains will be available for lookup operations.
 @warning Allocator reference must remain valid for mapper lifetime.
 @see GetDomainRange, ResolveDomain
*/
DomainIndexMapper::DomainIndexMapper(
  const oxygen::graphics::DescriptorAllocator& allocator,
  std::initializer_list<DomainKey> domains)
  : pimpl_(std::make_unique<DomainIndexMapperImpl>())
{
  for (auto const& d : domains) {
    const auto base = allocator.GetDomainBaseIndex(d.view_type, d.visibility);
    const auto allocated
      = allocator.GetAllocatedDescriptorsCount(d.view_type, d.visibility);
    const auto remaining
      = allocator.GetRemainingDescriptorsCount(d.view_type, d.visibility);
    const auto u_allocated = allocated.get();
    const auto u_remaining = remaining.get();
    const auto cap = oxygen::bindless::Capacity { u_allocated + u_remaining };
    pimpl_->map.emplace(d, DomainRange { .start = base, .capacity = cap });
  }
}

DomainIndexMapper::~DomainIndexMapper() = default;

/*!
 Returns the absolute range for the specified domain based on allocator state
 captured at construction time. Returns empty optional if the domain was not
 specified in the constructor's domain list.

 @param k Domain key specifying resource type and visibility
 @return Absolute range if domain was registered, empty optional otherwise

 ### Performance Characteristics

 - Time Complexity: O(1) average case for hash map lookup
 - Memory: No allocation
 - Optimization: Uses hash map for constant-time domain lookup

 ### Usage Examples

 ```cpp
 auto key = DomainKey{ResourceViewType::Texture2D,
                      DescriptorVisibility::Pixel};
 if (auto range = mapper.GetDomainRange(key)) {
   auto start_idx = range->start.get();
   auto slot_count = range->capacity.get();
 }
 ```

 @warning Results reflect allocator state at mapper construction time.
 @see ResolveDomain, DomainKey, DomainRange
*/
auto DomainIndexMapper::GetDomainRange(DomainKey const& k) const noexcept
  -> std::optional<DomainRange>
{
  if (auto it = pimpl_->map.find(k); it != pimpl_->map.end()) {
    return it->second;
  }
  return std::nullopt;
}

/*!
 Performs reverse lookup to find which registered domain contains the specified
 absolute handle index. Only searches domains that were specified at
 construction time.

 @param index Absolute handle index in the global descriptor heap
 @return Domain key if handle falls within a registered domain, empty optional
         otherwise

 ### Performance Characteristics

 - Time Complexity: O(d) where d is number of registered domains
 - Memory: No allocation
 - Optimization: Early termination on first match

 ### Usage Examples

 ```cpp
 auto handle = oxygen::bindless::HeapIndex{42};
 if (auto domain = mapper.ResolveDomain(handle)) {
   auto view_type = domain->view_type;
   auto visibility = domain->visibility;
 }
 ```

 @note Only searches domains specified at construction time.
 @warning Returns empty optional if handle falls outside registered domains.
 @see GetDomainRange, DomainKey
*/
auto DomainIndexMapper::ResolveDomain(
  oxygen::bindless::HeapIndex index) const noexcept -> std::optional<DomainKey>
{
  const auto u_index = index.get();
  for (auto const& entry : pimpl_->map) {
    const auto& k = entry.first;
    const auto& r = entry.second;
    const auto u_start = r.start.get();
    const auto u_capacity = r.capacity.get();
    if (u_index >= u_start && u_index < u_start + u_capacity) {
      return k;
    }
  }
  return std::nullopt;
}
