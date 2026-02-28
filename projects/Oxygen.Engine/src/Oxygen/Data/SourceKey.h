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

//! Unique identifier for a content source (PAK file or loose cooked folder).
/*!
 A 128-bit GUID that uniquely identifies a content source. This is used to
 ensure that resources are correctly scoped and cached even when content sources
 are mounted/unmounted or reordered.
*/
struct SourceKey : public NamedType<Uuid, struct SourceKeyTag,
                     // clang-format off
      Comparable,
      Printable,
      Hashable> // clang-format on
{
  static constexpr std::size_t kSizeBytes = Uuid::kSize;

  // NOLINTNEXTLINE(*-magic-numbers)
  using Base = NamedType<Uuid, struct SourceKeyTag,
    // clang-format off
    oxygen::Comparable,
    oxygen::Printable,
    oxygen::Hashable>; // clang-format on

  using Base::Base;

  constexpr explicit SourceKey(
    const std::array<std::uint8_t, kSizeBytes>& bytes) noexcept
    : Base(Uuid { bytes })
  {
  }

  [[nodiscard]] constexpr auto IsNil() const noexcept -> bool
  {
    return get().IsNil();
  }

  //! Create a SourceKey from a C-style byte array.
  static auto FromBytes(const std::array<std::uint8_t, kSizeBytes>& bytes)
    -> SourceKey
  {
    return SourceKey { Uuid { bytes } };
  }
};

//! String representation of SourceKey.
[[nodiscard]] inline auto to_string(const SourceKey& key) -> std::string
{
  return nostd::to_string(key.get());
}

//! Read-only byte span view for binary serialization.
[[nodiscard]] inline auto as_bytes(const SourceKey& key) noexcept
  -> std::span<const std::byte, SourceKey::kSizeBytes>
{
  return nostd::as_bytes(key.get());
}

//! Writable byte span view for binary serialization.
[[nodiscard]] inline auto as_writable_bytes(SourceKey& key) noexcept
  -> std::span<std::byte, SourceKey::kSizeBytes>
{
  return nostd::as_writable_bytes(key.get());
}

} // namespace oxygen::data

namespace std {
template <> struct hash<oxygen::data::SourceKey> {
  [[nodiscard]] auto operator()(
    const oxygen::data::SourceKey& key) const noexcept -> size_t
  {
    return static_cast<size_t>(
      oxygen::ComputeFNV1a64(key.get().data(), key.get().size()));
  }
};
} // namespace std
