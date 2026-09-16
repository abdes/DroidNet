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
#include <Oxygen/Base/Result.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Opaque identity of a material slot, scoped by its geometry asset identity.
/*!
 IDs carry no UUID version or time semantics. Producers allocate them from
 retained native provenance; labels, binding ordinals and material equality do
 not establish identity. The nil value is an unset sentinel, rejected by slot
 inventory validation.
*/
class MaterialSlotId final {
public:
  using value_type = std::uint8_t;
  using size_type = std::size_t;
  static constexpr size_type kSizeBytes = 16U;
  using ByteArray = std::array<value_type, kSizeBytes>;

  constexpr MaterialSlotId() noexcept = default;

  [[nodiscard]] constexpr auto IsNil() const noexcept -> bool
  {
    return bytes_ == ByteArray {};
  }

  [[nodiscard]] static constexpr auto FromBytes(const ByteArray& bytes) noexcept
    -> MaterialSlotId
  {
    return MaterialSlotId(bytes);
  }

  //! Parses only canonical lowercase UUID text; inventory validation rejects
  //! nil.
  OXGN_DATA_NDAPI static auto FromString(std::string_view text)
    -> Result<MaterialSlotId>;

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
    const MaterialSlotId&, const MaterialSlotId&) noexcept = default;
  [[nodiscard]] friend constexpr bool operator==(
    const MaterialSlotId&, const MaterialSlotId&) noexcept = default;

private:
  constexpr explicit MaterialSlotId(const ByteArray& bytes) noexcept
    : bytes_(bytes)
  {
  }

  ByteArray bytes_ {};
};

static_assert(sizeof(MaterialSlotId) == MaterialSlotId::kSizeBytes);
static_assert(std::is_trivially_copyable_v<MaterialSlotId>);
static_assert(std::is_standard_layout_v<MaterialSlotId>);

OXGN_DATA_NDAPI auto to_string(const MaterialSlotId& value) -> std::string;

[[nodiscard]] inline auto as_bytes(const MaterialSlotId& id) noexcept
  -> std::span<const std::byte, MaterialSlotId::kSizeBytes>
{
  return std::as_bytes(
    std::span<const MaterialSlotId::value_type, MaterialSlotId::kSizeBytes>(
      id.data(), id.size()));
}

} // namespace oxygen::data

namespace std {
template <> struct hash<oxygen::data::MaterialSlotId> {
  [[nodiscard]] auto operator()(
    const oxygen::data::MaterialSlotId& id) const noexcept -> size_t
  {
    return static_cast<size_t>(oxygen::ComputeFNV1a64(id.data(), id.size()));
  }
};
} // namespace std

template <>
struct fmt::formatter<oxygen::data::MaterialSlotId>
  : fmt::formatter<std::string> {
  constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

  auto format(
    const oxygen::data::MaterialSlotId& id, fmt::format_context& ctx) const
  {
    return fmt::formatter<std::string>::format(
      oxygen::data::to_string(id), ctx);
  }
};
