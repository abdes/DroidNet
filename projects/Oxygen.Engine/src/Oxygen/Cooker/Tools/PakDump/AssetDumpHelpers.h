//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "DumpContext.h"
#include "PrintUtils.h"
#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Tools/Common/EnumString.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pakdump::asset_dump_helpers {

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

//! The integer as `0x` and its lowercase hexadecimal digits.
template <std::integral T>
[[nodiscard]] inline auto ToHexString(const T value) -> std::string
{
  return fmt::format("{:#x}", value);
}

//! The digest as `0x` and its 64 lowercase hexadecimal digits.
[[nodiscard]] inline auto ToHexString(
  const oxygen::data::pak::core::ContentHashDigest& value) -> std::string
{
  return "0x" + base::ToHexString(value);
}

inline auto PrintAssetDescriptorHexPreview(
  const std::span<const std::byte> data, DumpContext& ctx) -> void
{
  if (!ctx.show_asset_descriptors) {
    return;
  }

  ctx.Out() << "    Asset Descriptor Preview (" << data.size()
            << " bytes read):\n";
  PrintUtils::HexDump(ctx.Out(), data, ctx.max_data_bytes);
}

//! Resolves a string of a descriptor's string table, for display.
[[nodiscard]] inline auto DescriptorString(
  const std::span<const std::byte> data,
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

inline auto PrintAssetMetadata(std::ostream& out,
  const oxygen::data::pak::core::AssetDirectoryEntry& e) -> void
{
  out << "    --- asset metadata ---\n";
  PrintUtils::Field(out, "Asset Type", toolkit::EnumString(e.asset_type));
  PrintUtils::Field(out, "Entry Offset", ToHexString(e.entry_offset));
  PrintUtils::Field(out, "Desc Offset", ToHexString(e.desc_offset));
  PrintUtils::Field(out, "Desc Size", std::to_string(e.desc_size) + " bytes");
}

//! The descriptor bytes of `entry`, a view of the mapped pak; empty when the
//! entry lies outside the file.
[[nodiscard]] inline auto TryDescriptorBytes(
  const oxygen::content::PakFile& pak,
  const oxygen::data::pak::core::AssetDirectoryEntry& entry)
  -> std::optional<std::span<const std::byte>>
{
  try {
    return pak.DescriptorBytes(entry);
  } catch (const std::out_of_range&) {
    return std::nullopt;
  }
}

} // namespace oxygen::content::pakdump::asset_dump_helpers
