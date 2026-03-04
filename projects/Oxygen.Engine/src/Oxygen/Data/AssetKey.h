//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Strongly typed 128-bit identity for a cooked/runtime asset.
class AssetKey final {
public:
  using value_type = std::uint8_t;
  using size_type = std::size_t;
  static constexpr size_type kSizeBytes = 16U;
  using ByteArray = std::array<value_type, kSizeBytes>;

  //! Nil key sentinel (all bytes set to zero).
  constexpr AssetKey() noexcept = default;

  [[nodiscard]] static constexpr auto Nil() noexcept -> AssetKey
  {
    return AssetKey {};
  }

  [[nodiscard]] constexpr auto IsNil() const noexcept -> bool
  {
    return bytes_ == ByteArray {};
  }

  [[nodiscard]] static constexpr auto FromBytes(const ByteArray& bytes) noexcept
    -> AssetKey
  {
    return AssetKey(bytes);
  }

  //! Computes a deterministic AssetKey from a canonical virtual path.
  OXGN_DATA_NDAPI static auto FromVirtualPath(std::string_view virtual_path)
    -> AssetKey;

  [[nodiscard]] constexpr auto data() const noexcept -> const value_type*
  {
    return bytes_.data();
  }

  [[nodiscard]] static constexpr auto size() noexcept -> size_type
  {
    return kSizeBytes;
  }

  [[nodiscard]] constexpr auto begin() const noexcept { return bytes_.begin(); }
  [[nodiscard]] constexpr auto end() const noexcept { return bytes_.end(); }

  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(
    const AssetKey&, const AssetKey&) noexcept
    = default;
  [[nodiscard]] friend constexpr bool operator==(
    const AssetKey&, const AssetKey&) noexcept
    = default;

private:
  constexpr explicit AssetKey(const ByteArray& bytes) noexcept
    : bytes_(bytes)
  {
  }

  ByteArray bytes_ {};
};
static_assert(sizeof(AssetKey) == AssetKey::kSizeBytes);
static_assert(std::is_trivially_copyable_v<AssetKey>);
static_assert(std::is_standard_layout_v<AssetKey>);

//! String representation of AssetKey.
OXGN_DATA_NDAPI auto to_string(const AssetKey& value) -> std::string;

//! Read-only byte span view for binary serialization.
[[nodiscard]] inline auto as_bytes(const AssetKey& key) noexcept
  -> std::span<const std::byte, AssetKey::kSizeBytes>
{
  return std::as_bytes(
    std::span<const AssetKey::value_type, AssetKey::kSizeBytes>(
      key.data(), key.size()));
}

} // namespace oxygen::data

namespace std {
template <> struct hash<oxygen::data::AssetKey> {
  [[nodiscard]] auto operator()(
    const oxygen::data::AssetKey& key) const noexcept -> size_t
  {
    return static_cast<size_t>(oxygen::ComputeFNV1a64(key.data(), key.size()));
  }
};
} // namespace std

template <>
struct fmt::formatter<oxygen::data::AssetKey> : fmt::formatter<std::string> {
  constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

  auto format(const oxygen::data::AssetKey& key, fmt::format_context& ctx) const
  {
    return fmt::formatter<std::string>::format(
      oxygen::data::to_string(key), ctx);
  }
};
