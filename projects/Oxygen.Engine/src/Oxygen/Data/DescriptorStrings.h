//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! A descriptor's string table, validated and resolvable by offset.
/*!
 Holds the bytes of a `pak::core::DescriptorStringTable`. A cooked table starts
 and ends with `\0`, so offset `0` is the empty string and every string is
 terminated. An empty table (a descriptor built in memory, with no strings)
 resolves only offset `0`.

 @see pak::core::DescriptorStringTable, DescriptorStringsBuilder
*/
class DescriptorStrings final {
public:
  DescriptorStrings() = default;

  //! Takes a table's bytes.
  /*!
   @throw std::invalid_argument if a non-empty table does not start and end
     with `\0`.
  */
  OXGN_DATA_API explicit DescriptorStrings(std::vector<char> table);

  //! Copies the table a descriptor's header locates in its bytes.
  /*!
   @param descriptor The complete descriptor, starting at its `AssetHeader`.
   @param table The header's string table location.
   @throw std::invalid_argument if the table is empty, lies outside the
     descriptor or inside its header, or is malformed.
  */
  OXGN_DATA_NDAPI static auto FromDescriptor(
    std::span<const std::byte> descriptor,
    const pak::core::DescriptorStringTable& table) -> DescriptorStrings;

  //! Checks that a cooked table's location fits a descriptor of `size` bytes.
  OXGN_DATA_NDAPI static auto IsValidLocation(
    const pak::core::DescriptorStringTable& table, std::size_t size) noexcept
    -> bool;

  //! Resolves the string at `offset`; nullopt when it is not in the table.
  OXGN_DATA_NDAPI auto TryGet(
    pak::core::StringTableOffsetT offset) const noexcept
    -> std::optional<std::string_view>;

  //! Resolves the string at `offset`.
  /*!
   @throw std::invalid_argument when the offset is not in the table.
  */
  OXGN_DATA_NDAPI auto Get(pak::core::StringTableOffsetT offset) const
    -> std::string_view;

  //! The table's bytes.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const char>
  {
    return table_;
  }

private:
  std::vector<char> table_;
};

//! Builds a descriptor's string table, storing each distinct string once.
/*!
 The table starts with `\0`, so the empty string is offset `0`. Strings are
 UTF-8 and cannot hold `\0`.
*/
class DescriptorStringsBuilder final {
public:
  OXGN_DATA_API DescriptorStringsBuilder();

  //! Continues an existing table, such as one being patched: its strings keep
  //! their offsets.
  OXGN_DATA_API explicit DescriptorStringsBuilder(
    const DescriptorStrings& existing);

  //! Returns the offset of `value`, adding it on first use.
  /*!
   @throw std::invalid_argument if `value` holds `\0`.
   @throw std::length_error if the table would exceed the 32-bit offset range.
  */
  OXGN_DATA_NDAPI auto Add(std::string_view value)
    -> pak::core::StringTableOffsetT;

  //! The table's bytes.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return std::as_bytes(std::span { bytes_ });
  }

  //! The table's size, in bytes.
  [[nodiscard]] auto Size() const noexcept -> pak::core::StringTableSizeT
  {
    return static_cast<pak::core::StringTableSizeT>(bytes_.size());
  }

  //! The table for readers of the descriptor being built.
  [[nodiscard]] auto Build() const -> DescriptorStrings
  {
    return DescriptorStrings { bytes_ };
  }

private:
  std::vector<char> bytes_;
  std::unordered_map<std::string, pak::core::StringTableOffsetT> offsets_;
};

//! Appends `strings` to a serialized descriptor and records its location.
/*!
 `descriptor` starts with its `AssetHeader`; the table is appended at its end
 and `header.strings` is patched in place. Call before hashing the descriptor.

 @throw std::invalid_argument if `descriptor` is smaller than an `AssetHeader`.
 @throw std::length_error if the table would start past the 32-bit offset
   range.
*/
OXGN_DATA_API auto AppendDescriptorStrings(std::vector<std::byte>& descriptor,
  const DescriptorStringsBuilder& strings) -> void;

//! Serializes a descriptor made of one packed record and its string table.
template <typename Descriptor>
  requires std::is_trivially_copyable_v<Descriptor>
[[nodiscard]] auto MakeDescriptorBytes(const Descriptor& record,
  const DescriptorStringsBuilder& strings = {}) -> std::vector<std::byte>
{
  const auto view = std::as_bytes(std::span { &record, 1U });
  auto bytes = std::vector<std::byte>(view.begin(), view.end());
  AppendDescriptorStrings(bytes, strings);
  return bytes;
}

} // namespace oxygen::data
