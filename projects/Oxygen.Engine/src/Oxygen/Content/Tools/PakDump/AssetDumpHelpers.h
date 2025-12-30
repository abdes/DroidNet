//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

#include "DumpContext.h"
#include "PrintUtils.h"

namespace oxygen::content::pakdump::asset_dump_helpers {

[[nodiscard]] inline auto GetAssetTypeName(const uint8_t asset_type) -> const
  char*
{
  return nostd::to_string(static_cast<oxygen::data::AssetType>(asset_type));
}

inline auto PrintAssetKey(const oxygen::data::AssetKey& key, DumpContext& ctx)
  -> void
{
  if (ctx.verbose) {
    PrintUtils::Field("GUID", oxygen::data::to_string(key));
    PrintUtils::Bytes(
      "Raw bytes", reinterpret_cast<const uint8_t*>(&key), sizeof(key));
    return;
  }

  PrintUtils::Field("GUID", oxygen::data::to_string(key));
}

template <typename T>
[[nodiscard]] inline auto ToHexString(T value) -> std::string
{
  std::ostringstream oss;
  oss << "0x" << std::hex << value;
  return oss.str();
}

inline auto PrintAssetDescriptorHexPreview(
  const std::vector<std::byte>& data, DumpContext& ctx) -> void
{
  if (!ctx.show_asset_descriptors) {
    return;
  }

  std::cout << "    Asset Descriptor Preview (" << data.size()
            << " bytes read):\n";
  PrintUtils::HexDump(reinterpret_cast<const uint8_t*>(data.data()),
    (std::min)(data.size(), ctx.max_data_bytes), ctx.max_data_bytes);
}

inline auto PrintAssetHeaderFields(
  const oxygen::data::pak::v1::AssetHeader& h, const int indent = 8) -> void
{
  PrintUtils::Field("Asset Type", static_cast<int>(h.asset_type), indent);
  PrintUtils::Field(
    "Name", std::string(h.name, strnlen(h.name, sizeof(h.name))), indent);
  PrintUtils::Field("Version", static_cast<int>(h.version), indent);
  PrintUtils::Field(
    "Streaming Priority", static_cast<int>(h.streaming_priority), indent);
  PrintUtils::Field("Content Hash", ToHexString(h.content_hash), indent);
  PrintUtils::Field("Variant Flags", ToHexString(h.variant_flags), indent);
}

[[nodiscard]] inline auto FormatVec3(const float v[3]) -> std::string
{
  return fmt::format("[{:.3f}, {:.3f}, {:.3f}]", v[0], v[1], v[2]);
}

[[nodiscard]] inline auto FormatQuat(const float q[4]) -> std::string
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
  std::memcpy(out, data.data() + offset, struct_size);
  return true;
}

[[nodiscard]] inline auto TryGetSceneString(
  std::string_view string_table, const uint32_t offset) -> std::string_view
{
  if (offset >= string_table.size()) {
    return {};
  }
  const auto* start = string_table.data() + offset;
  const size_t max_len = string_table.size() - offset;
  const size_t len = strnlen(start, max_len);
  return { start, len };
}

inline auto PrintAssetMetadata(
  const oxygen::data::pak::v1::AssetDirectoryEntry& e) -> void
{
  std::cout << "    --- asset metadata ---\n";
  PrintUtils::Field("Asset Type",
    std::string(GetAssetTypeName(e.asset_type)) + " ("
      + std::to_string(e.asset_type) + ")");
  PrintUtils::Field("Entry Offset", ToHexString(e.entry_offset));
  PrintUtils::Field("Desc Offset", ToHexString(e.desc_offset));
  PrintUtils::Field("Desc Size", std::to_string(e.desc_size) + " bytes");
}

[[nodiscard]] inline auto ReadDescriptorBytes(
  const oxygen::content::PakFile& pak,
  const oxygen::data::pak::v1::AssetDirectoryEntry& entry)
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
