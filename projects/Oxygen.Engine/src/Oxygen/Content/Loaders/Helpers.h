//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::loaders {

inline auto AddRangeEnd(size_t& end, const size_t offset, const size_t size)
  -> void
{
  if (size == 0) {
    end = std::max(end, offset);
    return;
  }

  const size_t candidate = offset + size;
  if (candidate < offset) {
    throw std::runtime_error("range overflow");
  }
  end = std::max(end, candidate);
}

inline auto CheckLoaderResult(const oxygen::Result<void>& result,
  const char* subject, const char* field) -> void
{
  if (!result) {
    LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
    throw std::runtime_error(fmt::format(
      "error reading {} ({}): {}", subject, field, result.error().message()));
  }
}

template <typename T>
inline auto CheckLoaderResult(const oxygen::Result<T>& result,
  const char* subject, const char* field) -> void
{
  if (!result) {
    LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
    throw std::runtime_error(fmt::format(
      "error reading {} ({}): {}", subject, field, result.error().message()));
  }
}

//=== Cooked IO Helpers ===--------------------------------------------------//

[[nodiscard]] inline auto ReadUnorm16(
  serio::AnyReader& reader, data::Unorm16& out) noexcept -> Result<void>
{
  uint16_t u_value = 0;
  auto result = reader.ReadInto(u_value);
  if (!result) {
    return ::oxygen::Err(result.error());
  }
  out = data::Unorm16 { u_value };
  return {};
}

[[nodiscard]] inline auto ReadHalfFloat(
  serio::AnyReader& reader, data::HalfFloat& out) noexcept -> Result<void>
{
  uint16_t u_value = 0;
  auto result = reader.ReadInto(u_value);
  if (!result) {
    return ::oxygen::Err(result.error());
  }
  out = data::HalfFloat { u_value };
  return {};
}

[[nodiscard]] inline auto WriteUnorm16(
  serio::AnyWriter& writer, const data::Unorm16 value) noexcept -> Result<void>
{
  const auto u_value = value.get();
  return writer.Write(u_value);
}

[[nodiscard]] inline auto WriteHalfFloat(serio::AnyWriter& writer,
  const data::HalfFloat value) noexcept -> Result<void>
{
  const auto u_value = value.get();
  return writer.Write(u_value);
}

//! Loads an AssetHeader, using the provided \b reader.
/*!

 @tparam S Stream type
 @param reader The reader to use for loading
 @return The loaded AssetHeader

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: No heap allocation
 - Optimization: Reads fields individually to avoid padding/representation
 issues

 @see oxygen::data::pak::core::AssetHeader
*/
inline auto LoadAssetHeader(
  serio::AnyReader& reader, data::pak::core::AssetHeader& header) -> void
{
  using data::AssetType;
  using data::pak::core::AssetHeader;

  LOG_SCOPE_F(1, "Header");

  auto result = reader.ReadInto<AssetHeader>(header);
  if (!result) {
    LOG_F(
      INFO, "-failed- on AssetHeader: {}", result.error().message().c_str());
    throw std::runtime_error(
      fmt::format("error reading asset header: {}", result.error().message()));
  }

  // Check validity of the header
  auto asset_type = header.asset_type;
  if (asset_type > static_cast<uint8_t>(AssetType::kMaxAssetType)) {
    LOG_F(ERROR, "-failed- on invalid asset type {}", asset_type);
    throw std::runtime_error(
      fmt::format("invalid asset type in header: {}", asset_type));
  }
  LOG_F(1, "asset type         : {}",
    nostd::to_string(static_cast<AssetType>(asset_type)));

  LOG_F(1, "string table       : {} bytes at {}", header.strings.size,
    header.strings.offset);
  LOG_F(1, "name offset        : {}", header.name_offset);

  LOG_F(1, "format version     : {}", header.version);
  LOG_F(1, "variant flags      : 0x{:08X}", header.variant_flags);
  LOG_F(1, "streaming priority : {}", header.streaming_priority);
  auto content_hash_hex = std::string {};
  content_hash_hex.reserve(header.content_hash.size() * 2);
  for (const auto byte : header.content_hash) {
    fmt::format_to(std::back_inserter(content_hash_hex), "{:02X}", byte);
  }
  LOG_F(1, "content hash       : {}", content_hash_hex);
}

//! Reads the string table of the descriptor that starts at `descriptor_start`.
/*!
 Leaves the reader where it was.

 @throw std::runtime_error when the table cannot be read or is malformed.
*/
inline auto ReadDescriptorStrings(serio::AnyReader& reader,
  const size_t descriptor_start, const data::pak::core::AssetHeader& header)
  -> data::DescriptorStrings
{
  const auto& table = header.strings;
  if (table.size == 0 || table.offset < sizeof(data::pak::core::AssetHeader)) {
    throw std::runtime_error("descriptor string table location is invalid");
  }
  const auto position = reader.Position();
  CheckLoaderResult(position, "descriptor", "position");
  CheckLoaderResult(reader.Seek(descriptor_start + table.offset), "descriptor",
    "string table offset");
  auto bytes = reader.ReadBlob(table.size);
  CheckLoaderResult(bytes, "descriptor", "string table");
  CheckLoaderResult(reader.Seek(*position), "descriptor", "position");

  auto chars = std::vector<char>(table.size);
  std::memcpy(chars.data(), bytes->data(), table.size);
  try {
    return data::DescriptorStrings { std::move(chars) };
  } catch (const std::invalid_argument& error) {
    throw std::runtime_error(error.what());
  }
}

//! Moves past the string table that follows a descriptor's records.
/*!
 Loaders consume their whole descriptor. A descriptor whose string table
 follows its records calls this after reading them.

 @throw std::runtime_error when the records do not end where the table starts.
*/
inline auto SkipDescriptorStrings(serio::AnyReader& reader,
  const size_t descriptor_start, const data::pak::core::AssetHeader& header)
  -> void
{
  const auto position = reader.Position();
  CheckLoaderResult(position, "descriptor", "position");
  if (*position != descriptor_start + header.strings.offset) {
    throw std::runtime_error(
      "descriptor records do not end where its string table starts");
  }
  CheckLoaderResult(
    reader.Forward(header.strings.size), "descriptor", "string table");
}

//! Resolves a string of a descriptor's string table.
/*!
 @throw std::runtime_error naming `field` when the offset is not in the table.
*/
inline auto ResolveDescriptorString(const data::DescriptorStrings& strings,
  const data::pak::core::StringTableOffsetT offset, const char* field)
  -> std::string
{
  const auto value = strings.TryGet(offset);
  if (!value) {
    throw std::runtime_error(
      fmt::format("{} offset {} is outside the string table", field, offset));
  }
  return std::string { *value };
}

} // namespace oxygen::content::loaders
