//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::internal {

//! Resource key that combines PAK file index, resource type index, and resource
//! index
/*!
 A 64-bit key that uniquely identifies a resource across all PAK files
 managed by an AssetLoader. The key layout is:

 - Upper 16 bits: PAK file index in the AssetLoader's collection
 - Next 16 bits: Resource type index (index in ResourceTypeList)
 - Lower 32 bits: ResourceIndexT from the PAK format (resource index within the
 PAK)

 This allows efficient lookup and type-safe handling of resources in a bindless
 system.

 @note This class is internal to AssetLoader implementation and should not
       be used directly by client code.
 */
class InternalResourceKey {
  static_assert(sizeof(content::ResourceKey) == sizeof(uint64_t),
    "ContentResource::Key must be 64 bits");

public:
  //! Construct a ResourceKey from PAK index, resource type index, and resource
  //! index
  constexpr InternalResourceKey(const uint16_t pak_index,
    const uint16_t resource_type_index,
    const data::pak::ResourceIndexT resource_index) noexcept
    : key_((static_cast<uint64_t>(pak_index) << 48)
        | (static_cast<uint64_t>(resource_type_index) << 32)
        | static_cast<uint64_t>(resource_index))
  {
    static_assert(sizeof(data::pak::ResourceIndexT) <= sizeof(uint32_t));
  }

  //! Construct from raw 64-bit key value
  explicit constexpr InternalResourceKey(const uint64_t raw_key) noexcept
    : key_(raw_key)
  {
  }

  //! Default constructor (invalid key)
  constexpr InternalResourceKey() noexcept
    : key_(0)
  {
  }

  //! Get the PAK file index (upper 16 bits of the key)
  [[nodiscard]] constexpr auto GetPakIndex() const noexcept -> uint16_t
  {
    return static_cast<uint16_t>(key_ >> 48);
  }

  //! Get the resource type index (bits 32-47 of the key)
  [[nodiscard]] constexpr auto GetResourceTypeIndex() const noexcept -> uint16_t
  {
    return static_cast<uint16_t>((key_ >> 32) & 0xFFFF);
  }

  //! Get the resource index within the PAK file (lower 32 bits of the key)
  [[nodiscard]] constexpr auto GetResourceIndex() const noexcept
    -> data::pak::ResourceIndexT
  {
    return static_cast<data::pak::ResourceIndexT>(key_ & 0xFFFFFFFF);
  }

  //! Get the raw 64-bit key value
  [[nodiscard]] constexpr auto GetRawKey() const noexcept -> ResourceKey
  {
    return static_cast<ResourceKey>(key_);
  }

  //! Equality comparison
  constexpr auto operator==(const InternalResourceKey& other) const noexcept
    -> bool
  {
    return key_ == other.key_;
  }

  //! Inequality comparison
  constexpr auto operator!=(const InternalResourceKey& other) const noexcept
    -> bool
  {
    return key_ != other.key_;
  }

  //! Less-than comparison for use in ordered containers
  constexpr auto operator<(const InternalResourceKey& other) const noexcept
    -> bool
  {
    return key_ < other.key_;
  }

private:
  uint64_t key_;
};

//! Convert ResourceKey to string for logging and debugging
inline auto to_string(const InternalResourceKey& key) -> std::string
{
  return "RK{pak:" + std::to_string(key.GetPakIndex())
    + ", type:" + std::to_string(key.GetResourceTypeIndex())
    + ", idx:" + std::to_string(key.GetResourceIndex()) + "}";
}

} // namespace oxygen::content::detail

//! Specialization of std::hash for ResourceKey
template <> struct std::hash<oxygen::content::internal::InternalResourceKey> {
  auto operator()(
    const oxygen::content::internal::InternalResourceKey& key) const noexcept
    -> size_t
  {
    return key.GetRawKey();
  }
};
