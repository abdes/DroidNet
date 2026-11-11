//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <iomanip>
#include <ranges>
#include <string_view>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "DecodedAsset.h"
#include "PrintUtils.h"
#include <fmt/format.h>

#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Cooker/Tools/Common/EnumString.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::pakdump {

class InputMappingContextAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;
    using oxygen::data::pak::input::InputMappingContextAssetDesc;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::TryDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    // The runtime reads it first; a descriptor it refuses is not printed.
    const auto context_asset
      = DecodeAsset(pak, entry, &loaders::LoadInputMappingContextAsset);
    const auto desc_read = serio::CopyAt<InputMappingContextAssetDesc>(*data);
    if (!desc_read) {
      ctx.Out() << "    InputMappingContextAssetDesc: (insufficient data)\n\n";
      co_return;
    }
    const auto& desc = *desc_read;

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), desc.header,
      asset_dump_helpers::DescriptorString(
        *data, desc.header, desc.header.name_offset),
      4);
    ctx.Out() << "    --- Input Mapping Context Descriptor Fields ---\n";
    PrintUtils::Field(ctx.Out(), "Flags", toolkit::EnumString(desc.flags), 8);
    PrintUtils::Field(ctx.Out(), "Mappings Count", desc.mappings.count, 8);
    PrintUtils::Field(
      ctx.Out(), "Mappings Entry Size", desc.mappings.entry_size, 8);
    PrintUtils::Field(ctx.Out(), "Triggers Count", desc.triggers.count, 8);
    PrintUtils::Field(
      ctx.Out(), "Trigger Aux Count", desc.trigger_aux.count, 8);

    if (!ctx.verbose) {
      ctx.Out() << "\n";
      co_return;
    }

    const auto mappings = context_asset->GetMappings();
    if (!mappings.empty()) {
      ctx.Out() << "    Mappings:\n";
      for (const auto& [i, record] : std::views::enumerate(mappings)) {
        const auto slot_name
          = context_asset->TryGetString(record.slot_name_offset)
              .value_or(std::string_view {});
        ctx.Out() << "      [" << i << "] action="
                  << oxygen::data::to_string(record.action_asset_key)
                  << " slot='" << slot_name << "'\n";
        PrintUtils::Field(ctx.Out(), "Trigger Range",
          fmt::format("[{}, {}]", record.trigger_start_index,
            record.trigger_start_index + record.trigger_count),
          10);
        PrintUtils::Field(
          ctx.Out(), "Flags", toolkit::EnumString(record.flags), 10);
        PrintUtils::Field(ctx.Out(), "Scale",
          fmt::format("[{:.3f}, {:.3f}]", record.scale[0], record.scale[1]),
          10);
        PrintUtils::Field(ctx.Out(), "Bias",
          fmt::format("[{:.3f}, {:.3f}]", record.bias[0], record.bias[1]), 10);
      }
    }

    const auto triggers = context_asset->GetTriggers();
    if (!triggers.empty()) {
      ctx.Out() << "    Triggers:\n";
      for (const auto& [i, record] : std::views::enumerate(triggers)) {
        ctx.Out() << "      [" << i
                  << "] type=" << toolkit::EnumString(record.type)
                  << " behavior=" << toolkit::EnumString(record.behavior)
                  << "\n";
        PrintUtils::Field(
          ctx.Out(), "Actuation Threshold", record.actuation_threshold, 10);
        PrintUtils::Field(ctx.Out(), "Linked Action",
          oxygen::data::to_string(record.linked_action_asset_key), 10);
        PrintUtils::Field(ctx.Out(), "Aux Range",
          fmt::format("[{}, {}]", record.aux_start_index,
            record.aux_start_index + record.aux_count),
          10);
      }
    }

    const auto aux_records = context_asset->GetTriggerAuxRecords();
    if (!aux_records.empty()) {
      ctx.Out() << "    Trigger Aux Records:\n";
      for (const auto& [i, record] : std::views::enumerate(aux_records)) {
        ctx.Out() << "      [" << i << "] action="
                  << oxygen::data::to_string(record.action_asset_key)
                  << " completion=0x" << std::hex << record.completion_states
                  << std::dec << " time_ns=" << record.time_to_complete_ns
                  << "\n";
      }
    }

    ctx.Out() << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
