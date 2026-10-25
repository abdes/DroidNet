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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! The 16 bytes of a `StableId`.
using StableIdBytes = std::array<std::uint8_t, 16>;

namespace detail {
  //! Parses canonical lowercase UUID text.
  OXGN_DATA_NDAPI auto ParseStableId(std::string_view text)
    -> Result<StableIdBytes>;

  //! Fresh random bytes.
  OXGN_DATA_NDAPI auto GenerateStableId() -> StableIdBytes;

  //! The leading bytes of the SHA-256 of `identity`; fails with
  //! `std::errc::invalid_argument` for an empty identity and
  //! `std::errc::result_out_of_range` for one that hashes to the nil value.
  OXGN_DATA_NDAPI auto HashStableIdentity(std::string_view identity)
    -> Result<StableIdBytes>;

  //! Canonical lowercase UUID text.
  OXGN_DATA_NDAPI auto FormatStableId(const StableIdBytes& bytes)
    -> std::string;
} // namespace detail

//! An opaque 128-bit identity, distinct per `Tag`.
/*!
 IDs carry no UUID version or time semantics. The nil value is an unset
 sentinel that each id's users reject where an id is required. `Tag` names the
 kind in `Tag::kName`, which errors use ("Surface", "Node").
*/
template <typename Tag> class StableId final {
public:
  using value_type = std::uint8_t;
  using size_type = std::size_t;
  static constexpr size_type kSizeBytes = 16U;
  using ByteArray = StableIdBytes;

  constexpr StableId() noexcept = default;

  [[nodiscard]] constexpr auto IsNil() const noexcept -> bool
  {
    return bytes_ == ByteArray {};
  }

  [[nodiscard]] static constexpr auto FromBytes(const ByteArray& bytes) noexcept
    -> StableId
  {
    return StableId(bytes);
  }

  //! Parses only canonical lowercase UUID text; callers reject nil.
  [[nodiscard]] static auto FromString(const std::string_view text)
    -> Result<StableId>
  {
    auto bytes = detail::ParseStableId(text);
    if (!bytes.has_value()) {
      return Result<StableId>::Err(bytes.error());
    }
    return Result<StableId>::Ok(FromBytes(bytes.value()));
  }

  //! Allocates a fresh identity.
  [[nodiscard]] static auto Generate() -> StableId
  {
    return FromBytes(detail::GenerateStableId());
  }

  //! Allocates deterministically from a producer-owned canonical identity.
  //! The identity includes the producer version and retained source namespace;
  //! display labels and unproven ordinals are not canonical identities.
  [[nodiscard]] static auto FromStableIdentity(const std::string_view identity)
    -> StableId
  {
    const auto bytes = detail::HashStableIdentity(identity);
    if (!bytes.has_value()) {
      throw std::invalid_argument(std::string(Tag::kName)
        + (bytes.error() == std::errc::invalid_argument
            ? " identity must not be empty"
            : " identity hash must not be nil"));
    }
    return FromBytes(bytes.value());
  }

  [[nodiscard]] constexpr auto Bytes() const noexcept -> const ByteArray&
  {
    return bytes_;
  }

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

  [[nodiscard]] friend constexpr auto operator<=>(const StableId&,
    const StableId&) noexcept -> std::strong_ordering = default;
  [[nodiscard]] friend constexpr auto operator==(
    const StableId&, const StableId&) noexcept -> bool = default;

private:
  constexpr explicit StableId(const ByteArray& bytes) noexcept
    : bytes_(bytes)
  {
  }

  ByteArray bytes_ {};
};

template <typename Tag>
[[nodiscard]] auto to_string(const StableId<Tag>& value) -> std::string
{
  return detail::FormatStableId(value.Bytes());
}

template <typename Tag>
[[nodiscard]] auto as_bytes(const StableId<Tag>& id) noexcept
  -> std::span<const std::byte, StableId<Tag>::kSizeBytes>
{
  return std::as_bytes(std::span<const std::uint8_t, StableId<Tag>::kSizeBytes>(
    id.data(), id.size()));
}

} // namespace oxygen::data

template <typename Tag> struct std::hash<oxygen::data::StableId<Tag>> {
  [[nodiscard]] auto operator()(
    const oxygen::data::StableId<Tag>& id) const noexcept -> size_t
  {
    return static_cast<size_t>(oxygen::ComputeFNV1a64(id.data(), id.size()));
  }
};

template <typename Tag>
struct fmt::formatter<oxygen::data::StableId<Tag>>
  : fmt::formatter<std::string> {
  // The text formats as a string, with a string's specs.
  auto format(
    const oxygen::data::StableId<Tag>& id, fmt::format_context& ctx) const
  {
    return fmt::formatter<std::string>::format(
      oxygen::data::to_string(id), ctx);
  }
};
