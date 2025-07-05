//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::detail {

//! Resource key that combines PAK file index and resource index
/*!
 A 64-bit key that uniquely identifies a resource across all PAK files
 managed by an AssetLoader. The upper 32 bits contain the PAK file index
 in the AssetLoader's collection, and the lower 32 bits contain the
 ResourceIndexT from the PAK format.

 @note This class is internal to AssetLoader implementation and should not
       be used directly by client code.
 */
class ResourceKey {
public:
  //! Construct from PAK index and resource index
  constexpr ResourceKey(const uint32_t pak_index,
    const data::pak::ResourceIndexT resource_index) noexcept
    : key_((static_cast<uint64_t>(pak_index) << 32) | resource_index)
  {
    static_assert(sizeof(data::pak::ResourceIndexT) <= sizeof(uint32_t));
  }

  //! Construct from raw 64-bit key value
  explicit constexpr ResourceKey(const uint64_t raw_key) noexcept
    : key_(raw_key)
  {
  }

  //! Default constructor (invalid key)
  constexpr ResourceKey() noexcept
    : key_(0)
  {
  }

  //! Get the PAK file index (upper 32 bits)
  [[nodiscard]] constexpr auto GetPakIndex() const noexcept -> uint32_t
  {
    return static_cast<uint32_t>(key_ >> 32);
  }

  //! Get the resource index within the PAK file (lower 32 bits)
  [[nodiscard]] constexpr auto GetResourceIndex() const noexcept
    -> data::pak::ResourceIndexT
  {
    return key_ & 0xFFFFFFFF;
  }

  //! Get the raw 64-bit key value
  [[nodiscard]] constexpr auto GetRawKey() const noexcept -> uint64_t
  {
    return key_;
  }

  //! Equality comparison
  constexpr auto operator==(const ResourceKey& other) const noexcept -> bool
  {
    return key_ == other.key_;
  }

  //! Inequality comparison
  constexpr auto operator!=(const ResourceKey& other) const noexcept -> bool
  {
    return key_ != other.key_;
  }

  //! Less-than comparison for use in ordered containers
  constexpr auto operator<(const ResourceKey& other) const noexcept -> bool
  {
    return key_ < other.key_;
  }

private:
  uint64_t key_;
};

//! Convert ResourceKey to string for logging and debugging
inline auto to_string(const ResourceKey& key) -> std::string
{
  return "RK{pak:" + std::to_string(key.GetPakIndex())
    + ", idx:" + std::to_string(key.GetResourceIndex()) + "}";
}

} // namespace oxygen::content::detail

//! Specialization of std::hash for ResourceKey
template <> struct std::hash<oxygen::content::detail::ResourceKey> {
  auto operator()(
    const oxygen::content::detail::ResourceKey& key) const noexcept -> size_t
  {
    return key.GetRawKey();
  }
};
