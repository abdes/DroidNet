//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

//! Opaque loader-lifetime identifier for a resource.
/*!
 Minted by AssetLoader from a full cooked or synthetic identity. The value is
 neither a hash nor an encoded source/index. Payload eviction preserves the
 locator while its source remains readable. Do not serialize runtime keys.

 @see AssetLoader::MakeResourceKey
*/
// clang-format off
class ResourceKey : public NamedType<uint64_t, struct ResourceKeyTag,
	DefaultInitialized,
	Comparable,
	Printable> // clang-format on
{
  using Base = NamedType<uint64_t, struct ResourceKeyTag,
    // clang-format off
    oxygen::DefaultInitialized,
    oxygen::Comparable,
    oxygen::Printable>; // clang-format on

public:
  //! Reserved placeholder resource key.
  OXGN_CNTT_API static const ResourceKey kFallback;
  OXGN_CNTT_API static const ResourceKey kPlaceholder;

  // Inherit base constructors
  using Base::Base;

  [[nodiscard]] constexpr auto IsPlaceholder() const noexcept
  {
    return *this == kPlaceholder;
  }

  [[nodiscard]] constexpr auto IsFallback() const noexcept
  {
    return *this == kFallback;
  }
};

//! Convert a ResourceKey to string for logging.
OXGN_CNTT_NDAPI auto to_string(const ResourceKey& key) -> std::string;

} // namespace oxygen::content

template <> struct std::hash<oxygen::content::ResourceKey> {
  auto operator()(const oxygen::content::ResourceKey& k) const noexcept
    -> std::size_t
  {
    return std::hash<std::uint64_t>()(k.get());
  }
}; // namespace std
