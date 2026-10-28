//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstring>
#include <iomanip>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "DumpContext.h"
#include "PrintUtils.h"
#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::pakdump::asset_dump_helpers {

[[nodiscard]] inline auto GetAssetTypeName(
  const oxygen::data::AssetType asset_type) -> const char*
{
  return oxygen::data::to_string(asset_type);
}

inline auto PrintAssetKey(const oxygen::data::AssetKey& key, DumpContext& ctx)
  -> void
{
  if (ctx.verbose) {
    PrintUtils::Field(ctx.Out(), "GUID", oxygen::data::to_string(key));
    PrintUtils::Bytes(ctx.Out(), "Raw bytes",
      std::span<const uint8_t> { key.data(), oxygen::data::AssetKey::size() });
    return;
  }

  PrintUtils::Field(ctx.Out(), "GUID", oxygen::data::to_string(key));
}

template <typename T>
[[nodiscard]] inline auto ToHexString(T value) -> std::string
{
  std::ostringstream oss;
  oss << "0x" << std::hex << value;
  return oss.str();
}

[[nodiscard]] inline auto ToHexString(
  const oxygen::data::pak::core::ContentHashDigest& value) -> std::string
{
  std::ostringstream oss;
  oss << "0x";
  for (const auto byte : value) {
    oss << std::setw(2) << std::setfill('0') << std::hex
        << static_cast<unsigned>(byte);
  }
  return oss.str();
}

inline auto PrintAssetDescriptorHexPreview(
  const std::vector<std::byte>& data, DumpContext& ctx) -> void
{
  if (!ctx.show_asset_descriptors) {
    return;
  }

  ctx.Out() << "    Asset Descriptor Preview (" << data.size()
            << " bytes read):\n";
  PrintUtils::HexDump(ctx.Out(), data, ctx.max_data_bytes);
}

//! Resolves a string of a descriptor's string table, for display.
[[nodiscard]] inline auto DescriptorString(const std::vector<std::byte>& data,
  const oxygen::data::pak::core::AssetHeader& header,
  const oxygen::data::pak::core::StringTableOffsetT offset) -> std::string
{
  try {
    const auto strings
      = oxygen::data::DescriptorStrings::FromDescriptor(data, header.strings);
    if (const auto value = strings.TryGet(offset)) {
      return std::string { *value };
    }
  } catch (const std::invalid_argument&) {
    return "<invalid string table>";
  }
  return fmt::format("<invalid string offset {}>", offset);
}

inline auto PrintAssetHeaderFields(std::ostream& out,
  const oxygen::data::pak::core::AssetHeader& h, const std::string_view name,
  const int indent = 8) -> void
{
  PrintUtils::Field(out, "Name", std::string { name }, indent);
  PrintUtils::Field(out, "String Table",
    fmt::format("{} bytes at {}", h.strings.size, h.strings.offset), indent);
  PrintUtils::Field(out, "Format Version", static_cast<int>(h.version), indent);
  PrintUtils::Field(
    out, "Streaming Priority", static_cast<int>(h.streaming_priority), indent);
  PrintUtils::Field(out, "Content Hash", ToHexString(h.content_hash), indent);
  PrintUtils::Field(out, "Variant Flags", ToHexString(h.variant_flags), indent);
}

[[nodiscard]] inline auto TryReadAssetHeader(const std::vector<std::byte>& data)
  -> std::optional<oxygen::data::pak::core::AssetHeader>
{
  using oxygen::data::pak::core::AssetHeader;

  if (data.size() < sizeof(AssetHeader)) {
    return std::nullopt;
  }

  AssetHeader header {};
  std::memcpy(&header, data.data(), sizeof(header));
  return header;
}

[[nodiscard]] inline auto FormatVec3(const std::span<const float, 3> v)
  -> std::string
{
  return fmt::format("[{:.3f}, {:.3f}, {:.3f}]", v[0], v[1], v[2]);
}

[[nodiscard]] inline auto FormatQuat(const std::span<const float, 4> q)
  -> std::string
{
  return fmt::format(
    "[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", q[0], q[1], q[2], q[3]);
}

[[nodiscard]] inline auto ReadStructAt(const std::vector<std::byte>& data,
  const size_t offset, const size_t struct_size, void* out) -> bool
{
  if (offset > data.size() || struct_size > (data.size() - offset)) {
    return false;
  }
  std::memcpy(out, std::span(data).subspan(offset).data(), struct_size);
  return true;
}

inline auto PrintAssetMetadata(std::ostream& out,
  const oxygen::data::pak::core::AssetDirectoryEntry& e) -> void
{
  out << "    --- asset metadata ---\n";
  PrintUtils::Field(out, "Asset Type",
    std::string(GetAssetTypeName(e.asset_type)) + " ("
      + nostd::to_string(e.asset_type) + ")");
  PrintUtils::Field(out, "Entry Offset", ToHexString(e.entry_offset));
  PrintUtils::Field(out, "Desc Offset", ToHexString(e.desc_offset));
  PrintUtils::Field(out, "Desc Size", std::to_string(e.desc_size) + " bytes");
}

//! Copies the trivially copyable record of type `T` stored at `offset`.
//! The caller guarantees `offset + sizeof(T) <= bytes.size()`.
template <typename T>
  requires(
    std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
[[nodiscard]] inline auto ReadRecordAt(
  const std::span<const std::byte> bytes, const size_t offset) -> T
{
  T record {};
  std::memcpy(&record, bytes.subspan(offset, sizeof(T)).data(), sizeof(T));
  return record;
}

[[nodiscard]] inline auto ReadDescriptorBytes(
  const oxygen::content::PakFile& pak,
  const oxygen::data::pak::core::AssetDirectoryEntry& entry)
  -> std::optional<std::vector<std::byte>>
{
  try {
    auto reader = pak.CreateReader(entry);
    const auto bytes_to_read = static_cast<size_t>(entry.desc_size);
    auto data_result = reader.ReadBlob(bytes_to_read);
    if (!data_result.has_value()) {
      return std::nullopt;
    }
    return data_result.value();
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace oxygen::content::pakdump::asset_dump_helpers
