//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::internal {

//! Container-relative resource dependency reference.
/*!
 A `ResourceRef` is an identity-safe, trivially copyable reference to a cooked
 resource *relative to a mounted source*.

 It is used as a short-lived bridge between:

 - worker-thread decode (collects dependencies without calling AssetLoader), and
 - owning-thread publish (binds references into `content::ResourceKey`).

 Properties:

 - **Identity-safe**: contains no pointers, paths, readers, or locators.
 - **Thread-safe to copy**: trivially copyable POD-like shape.
 - **Not a public API**: internal-only and not a stable ABI.

 Binding rule (owning thread):

 1. Resolve `source` into the loader-owned 16-bit source id.
 2. Map `resource_type_id` into the `ResourceTypeList` index.
 3. Pack `(source_id, resource_type_index, resource_index)` into `ResourceKey`.

 @note This type intentionally carries `TypeId` instead of a template parameter
       so decode code can stay non-templated and avoid knowledge of the runtime
       key encoding.
 */
struct ResourceRef final {
  SourceToken source {};
  TypeId resource_type_id {};
  data::pak::core::ResourceIndexT resource_index {};

  constexpr auto operator==(const ResourceRef& other) const noexcept -> bool
  {
    return source == other.source && resource_type_id == other.resource_type_id
      && resource_index == other.resource_index;
  }
};

static_assert(std::is_trivially_copyable_v<ResourceRef>);

//! Convert a ResourceRef to string for logging.
inline auto to_string(const ResourceRef& ref) -> std::string
{
  return "ResourceRef{source=" + to_string(ref.source)
    + ", type=" + std::to_string(ref.resource_type_id)
    + ", index=" + std::to_string(ref.resource_index) + "}";
}

} // namespace oxygen::content::internal

template <> struct std::hash<oxygen::content::internal::ResourceRef> {
  auto operator()(
    const oxygen::content::internal::ResourceRef& ref) const noexcept -> size_t
  {
    size_t seed = 0;
    oxygen::HashCombine(seed, ref.source);
    oxygen::HashCombine(seed, ref.resource_type_id);
    oxygen::HashCombine(seed, ref.resource_index);
    return seed;
  }
};
