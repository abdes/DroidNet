//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Strongly typed UUID identity for a cooked/runtime asset.
struct AssetKey : public NamedType<Uuid, struct AssetKeyTag, // clang-format off
      Comparable,
      Printable,
      Hashable> // clang-format on
{
  static constexpr std::size_t kSizeBytes = Uuid::kSize;

  // NOLINTNEXTLINE(*-magic-numbers)
  using Base = NamedType<Uuid, struct AssetKeyTag, // clang-format off
    oxygen::Comparable,
    oxygen::Printable,
    oxygen::Hashable>; // clang-format on

  using Base::Base;

  constexpr explicit AssetKey(
    const std::array<std::uint8_t, kSizeBytes>& bytes) noexcept
    : Base(Uuid { bytes })
  {
  }

  [[nodiscard]] constexpr auto IsNil() const noexcept -> bool
  {
    return get().IsNil();
  }

  [[nodiscard]] static auto FromBytes(
    const std::array<std::uint8_t, kSizeBytes>& bytes) -> AssetKey
  {
    return AssetKey { Uuid { bytes } };
  }
};
static_assert(sizeof(AssetKey) == AssetKey::kSizeBytes);

//! String representation of AssetKey.
[[nodiscard]] inline auto to_string(const AssetKey& value) noexcept
  -> std::string
{
  return nostd::to_string(value.get());
}

//! Read-only byte span view for binary serialization.
[[nodiscard]] inline auto as_bytes(const AssetKey& key) noexcept
  -> std::span<const std::byte, AssetKey::kSizeBytes>
{
  return nostd::as_bytes(key.get());
}

//! Writable byte span view for binary serialization.
[[nodiscard]] inline auto as_writable_bytes(AssetKey& key) noexcept
  -> std::span<std::byte, AssetKey::kSizeBytes>
{
  return nostd::as_writable_bytes(key.get());
}

//! Generates a random UUIDv7 for asset identity assignment.
[[nodiscard]] inline auto GenerateAssetGuid() -> Uuid
{
  return Uuid::Generate();
}

} // namespace oxygen::data

namespace std {
template <> struct hash<oxygen::data::AssetKey> {
  [[nodiscard]] auto operator()(
    const oxygen::data::AssetKey& key) const noexcept -> size_t
  {
    return static_cast<size_t>(
      oxygen::ComputeFNV1a64(key.get().data(), key.get().size()));
  }
};
} // namespace std
