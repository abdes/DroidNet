//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptResource.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "PrintUtils.h"

namespace oxygen::content::pakdump {

class ScriptAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    using oxygen::data::ScriptResource;
    using oxygen::data::pak::ScriptAssetDesc;

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(ScriptAssetDesc)) {
      std::cout << "    ScriptAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    ScriptAssetDesc desc {};
    std::memcpy(&desc, data->data(), sizeof(desc));

    asset_dump_helpers::PrintAssetHeaderFields(desc.header, 4);
    std::cout << "    --- Script Descriptor Fields ---\n";
    PrintUtils::Field(
      "Bytecode Resource Index", desc.bytecode_resource_index, 8);
    PrintUtils::Field("Source Resource Index", desc.source_resource_index, 8);
    PrintUtils::Field("Flags",
      asset_dump_helpers::ToHexString(static_cast<uint32_t>(desc.flags)), 8);
    {
      const auto* const path_begin = std::begin(desc.external_source_path);
      const auto* const path_end = std::end(desc.external_source_path);
      const auto* const path_nul = std::find(path_begin, path_end, '\0');
      const std::string path(path_begin, path_nul);
      PrintUtils::Field(
        "External Source Path", path.empty() ? "<none>" : path, 8);
    }

    if (!ctx.verbose && !ctx.show_resource_data) {
      std::cout << "\n";
      co_return;
    }

    const auto selected_index = desc.bytecode_resource_index != 0
      ? desc.bytecode_resource_index
      : desc.source_resource_index;
    if (selected_index == 0) {
      PrintUtils::Field(
        "Resource Entry", "none (embedded payload not assigned)", 8);
      std::cout << "\n";
      co_return;
    }

    if (!pak.HasTableOf<ScriptResource>()) {
      PrintUtils::Field("Resource Entry", "Script table is not present", 8);
      std::cout << "\n";
      co_return;
    }

    auto& scripts_table = pak.ScriptsTable();
    const auto selected_resource_index
      = data::pak::ResourceIndexT { selected_index };
    if (!scripts_table.IsValidKey(selected_resource_index)) {
      PrintUtils::Field(
        "Resource Entry", "Index out of script table bounds", 8);
      std::cout << "\n";
      co_return;
    }

    try {
      const auto key = asset_loader.MakeResourceKey<ScriptResource>(
        pak, selected_resource_index);
      auto script_resource
        = co_await asset_loader.LoadResourceAsync<ScriptResource>(key);
      if (!script_resource) {
        PrintUtils::Field(
          "Resource Entry", "Failed to load script resource", 8);
        std::cout << "\n";
        co_return;
      }

      PrintUtils::Field("Resource Entry", selected_index, 8);
      PrintUtils::Field("Data Offset",
        asset_dump_helpers::ToHexString(script_resource->GetDataOffset()), 8);
      PrintUtils::Field("Data Size",
        std::to_string(script_resource->GetDataSize()) + " bytes", 8);
      PrintUtils::Field("Language",
        std::to_string(static_cast<uint32_t>(script_resource->GetLanguage())),
        8);
      PrintUtils::Field("Encoding",
        std::to_string(static_cast<uint32_t>(script_resource->GetEncoding())),
        8);
      PrintUtils::Field("Compression",
        std::to_string(
          static_cast<uint32_t>(script_resource->GetCompression())),
        8);
      PrintUtils::Field("Content Hash",
        asset_dump_helpers::ToHexString(script_resource->GetContentHash()), 8);

      if (ctx.show_resource_data) {
        const auto script_data = script_resource->GetData();
        const auto bytes_to_read
          = (std::min)(script_data.size(), ctx.max_data_bytes);
        std::cout << "        Script Data Preview (" << bytes_to_read << " of "
                  << script_data.size() << " bytes):\n";
        PrintUtils::HexDump(
          script_data.data(), bytes_to_read, ctx.max_data_bytes);
      }
    } catch (const std::exception& ex) {
      PrintUtils::Field(
        "Resource Entry", std::string("Error: ") + ex.what(), 8);
    }

    std::cout << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
