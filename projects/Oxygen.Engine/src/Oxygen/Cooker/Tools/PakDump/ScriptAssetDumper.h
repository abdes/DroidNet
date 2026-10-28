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
#include <string>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "PrintUtils.h"

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptResource.h>

namespace oxygen::content::pakdump {

class ScriptAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    using oxygen::data::ScriptResource;
    using oxygen::data::pak::scripting::ScriptAssetDesc;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(ScriptAssetDesc)) {
      ctx.Out() << "    ScriptAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    ScriptAssetDesc desc {};
    std::memcpy(&desc, data->data(), sizeof(desc));

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), desc.header,
      asset_dump_helpers::DescriptorString(
        *data, desc.header, desc.header.name_offset),
      4);
    ctx.Out() << "    --- Script Descriptor Fields ---\n";
    PrintUtils::Field(
      ctx.Out(), "Bytecode Reference", desc.bytecode_resource_index.get(), 8);
    PrintUtils::Field(
      ctx.Out(), "Source Reference", desc.source_resource_index.get(), 8);
    PrintUtils::Field(ctx.Out(), "Flags",
      asset_dump_helpers::ToHexString(static_cast<uint32_t>(desc.flags)), 8);
    {
      const auto path = asset_dump_helpers::DescriptorString(
        *data, desc.header, desc.external_source_path_offset);
      PrintUtils::Field(
        ctx.Out(), "External Source Path", path.empty() ? "<none>" : path, 8);
    }
    PrintUtils::Field(ctx.Out(), "Declared Parameters", desc.params.count, 8);

    if (!ctx.verbose && !ctx.show_resource_data) {
      ctx.Out() << "\n";
      co_return;
    }

    const auto selected_index
      = desc.bytecode_resource_index != data::kNoResourceReference
      ? desc.bytecode_resource_index
      : desc.source_resource_index;
    if (selected_index == data::kNoResourceReference) {
      PrintUtils::Field(
        ctx.Out(), "Resource Entry", "none (embedded payload not assigned)", 8);
      ctx.Out() << "\n";
      co_return;
    }

    if (!pak.HasTableOf<ScriptResource>()) {
      PrintUtils::Field(
        ctx.Out(), "Resource Entry", "Script table is not present", 8);
      ctx.Out() << "\n";
      co_return;
    }

    auto& scripts_table = pak.ScriptsTable();
    const auto references = pak.ReadAssetReferences(entry.asset_key);
    const auto resolved
      = references.ResolveResource(selected_index, data::ResourceKind::kScript);
    if (!resolved || !resolved->has_value()) {
      PrintUtils::Field(
        ctx.Out(), "Resource Entry", "Invalid script reference", 8);
      co_return;
    }
    const auto selected_resource_index = **resolved;
    if (!scripts_table.IsValidKey(selected_resource_index)) {
      PrintUtils::Field(
        ctx.Out(), "Resource Entry", "Index out of script table bounds", 8);
      ctx.Out() << "\n";
      co_return;
    }

    try {
      const auto key = asset_loader.MakeResourceKey<ScriptResource>(
        pak, selected_resource_index);
      auto script_resource
        = co_await asset_loader.LoadResourceAsync<ScriptResource>(key);
      if (!script_resource) {
        PrintUtils::Field(
          ctx.Out(), "Resource Entry", "Failed to load script resource", 8);
        ctx.Out() << "\n";
        co_return;
      }

      PrintUtils::Field(
        ctx.Out(), "Resource Entry", selected_resource_index.get(), 8);
      PrintUtils::Field(ctx.Out(), "Data Offset",
        asset_dump_helpers::ToHexString(script_resource->GetDataOffset()), 8);
      PrintUtils::Field(ctx.Out(), "Data Size",
        std::to_string(script_resource->GetDataSize()) + " bytes", 8);
      PrintUtils::Field(ctx.Out(), "Language",
        std::to_string(static_cast<uint32_t>(script_resource->GetLanguage())),
        8);
      PrintUtils::Field(ctx.Out(), "Encoding",
        std::to_string(static_cast<uint32_t>(script_resource->GetEncoding())),
        8);
      PrintUtils::Field(ctx.Out(), "Compression",
        std::to_string(
          static_cast<uint32_t>(script_resource->GetCompression())),
        8);
      PrintUtils::Field(ctx.Out(), "Content Hash",
        asset_dump_helpers::ToHexString(script_resource->GetContentHash()), 8);

      if (ctx.show_resource_data) {
        const auto script_data = script_resource->GetData();
        const auto bytes_to_read
          = (std::min)(script_data.size(), ctx.max_data_bytes);
        ctx.Out() << "        Script Data Preview (" << bytes_to_read << " of "
                  << script_data.size() << " bytes):\n";
        PrintUtils::HexDump(
          ctx.Out(), script_data.data(), bytes_to_read, ctx.max_data_bytes);
      }
    } catch (const std::exception& ex) {
      PrintUtils::Field(
        ctx.Out(), "Resource Entry", std::string("Error: ") + ex.what(), 8);
    }

    ctx.Out() << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
