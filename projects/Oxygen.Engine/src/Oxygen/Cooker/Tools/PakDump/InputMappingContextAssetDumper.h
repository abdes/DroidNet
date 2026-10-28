//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <string>
#include <string_view>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "PrintUtils.h"
#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::pakdump {

class InputMappingContextAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;
    using oxygen::data::pak::input::InputActionMappingRecord;
    using oxygen::data::pak::input::InputMappingContextAssetDesc;
    using oxygen::data::pak::input::InputTriggerAuxRecord;
    using oxygen::data::pak::input::InputTriggerRecord;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(InputMappingContextAssetDesc)) {
      ctx.Out() << "    InputMappingContextAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    InputMappingContextAssetDesc desc {};
    std::memcpy(&desc, data->data(), sizeof(desc));

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), desc.header,
      asset_dump_helpers::DescriptorString(
        *data, desc.header, desc.header.name_offset),
      4);
    ctx.Out() << "    --- Input Mapping Context Descriptor Fields ---\n";
    PrintUtils::Field(ctx.Out(), "Flags", nostd::to_string(desc.flags), 8);
    PrintUtils::Field(ctx.Out(), "Mappings Count", desc.mappings.count, 8);
    PrintUtils::Field(
      ctx.Out(), "Mappings Entry Size", desc.mappings.entry_size, 8);
    PrintUtils::Field(ctx.Out(), "Triggers Count", desc.triggers.count, 8);
    PrintUtils::Field(
      ctx.Out(), "Trigger Aux Count", desc.trigger_aux.count, 8);

    const auto ParseTable
      = [&](const oxygen::data::pak::input::InputDataTable& table,
          const size_t expected_size, const char* const table_name) -> bool {
      const auto offset = static_cast<size_t>(table.offset);
      const auto count = static_cast<size_t>(table.count);
      const auto size = static_cast<size_t>(table.entry_size);

      if (count == 0) {
        return true;
      }
      if (size != expected_size) {
        PrintUtils::Field(ctx.Out(), "Warning",
          fmt::format("{} entry size mismatch: {} != {}", table_name, size,
            expected_size),
          8);
        return false;
      }
      if (offset > data->size()) {
        PrintUtils::Field(ctx.Out(), "Warning",
          fmt::format("{} offset out of bounds", table_name), 8);
        return false;
      }
      const auto total_size = count * expected_size;
      if (total_size > (data->size() - offset)) {
        PrintUtils::Field(ctx.Out(), "Warning",
          fmt::format("{} table payload exceeds descriptor size", table_name),
          8);
        return false;
      }
      return true;
    };

    const auto mappings_ok
      = ParseTable(desc.mappings, sizeof(InputActionMappingRecord), "Mappings");
    const auto triggers_ok
      = ParseTable(desc.triggers, sizeof(InputTriggerRecord), "Triggers");
    const auto aux_ok = ParseTable(
      desc.trigger_aux, sizeof(InputTriggerAuxRecord), "TriggerAux");

    if (!ctx.verbose) {
      ctx.Out() << "\n";
      co_return;
    }

    if (mappings_ok && desc.mappings.count > 0) {
      ctx.Out() << "    Mappings:\n";
      const auto mapping_count = static_cast<size_t>(desc.mappings.count);
      const auto mapping_limit = ctx.verbose
        ? mapping_count
        : (std::min)(mapping_count, size_t { 16 });
      for (size_t i = 0; i < mapping_limit; ++i) {
        const auto record_offset = static_cast<size_t>(desc.mappings.offset)
          + (i * sizeof(InputActionMappingRecord));
        const auto record
          = asset_dump_helpers::ReadRecordAt<InputActionMappingRecord>(
            *data, record_offset);

        const auto slot_name = asset_dump_helpers::DescriptorString(
          *data, desc.header, record.slot_name_offset);
        ctx.Out() << "      [" << i << "] action="
                  << oxygen::data::to_string(record.action_asset_key)
                  << " slot='" << slot_name << "'\n";
        PrintUtils::Field(ctx.Out(), "Trigger Range",
          fmt::format("[{}, {}]", record.trigger_start_index,
            record.trigger_start_index + record.trigger_count),
          10);
        PrintUtils::Field(
          ctx.Out(), "Flags", nostd::to_string(record.flags), 10);
        PrintUtils::Field(ctx.Out(), "Scale",
          fmt::format("[{:.3f}, {:.3f}]", record.scale[0], record.scale[1]),
          10);
        PrintUtils::Field(ctx.Out(), "Bias",
          fmt::format("[{:.3f}, {:.3f}]", record.bias[0], record.bias[1]), 10);
      }
      if (mapping_count > mapping_limit) {
        ctx.Out() << "      ... (" << (mapping_count - mapping_limit)
                  << " more mappings)\n";
      }
    }

    if (triggers_ok && desc.triggers.count > 0) {
      ctx.Out() << "    Triggers:\n";
      const auto trigger_count = static_cast<size_t>(desc.triggers.count);
      const auto trigger_limit = ctx.verbose
        ? trigger_count
        : (std::min)(trigger_count, size_t { 16 });
      for (size_t i = 0; i < trigger_limit; ++i) {
        const auto record_offset = static_cast<size_t>(desc.triggers.offset)
          + (i * sizeof(InputTriggerRecord));
        const auto record
          = asset_dump_helpers::ReadRecordAt<InputTriggerRecord>(
            *data, record_offset);

        ctx.Out() << "      [" << i
                  << "] type=" << nostd::to_string(record.type)
                  << " behavior=" << nostd::to_string(record.behavior) << "\n";
        PrintUtils::Field(
          ctx.Out(), "Actuation Threshold", record.actuation_threshold, 10);
        PrintUtils::Field(ctx.Out(), "Linked Action",
          oxygen::data::to_string(record.linked_action_asset_key), 10);
        PrintUtils::Field(ctx.Out(), "Aux Range",
          fmt::format("[{}, {}]", record.aux_start_index,
            record.aux_start_index + record.aux_count),
          10);
      }
      if (trigger_count > trigger_limit) {
        ctx.Out() << "      ... (" << (trigger_count - trigger_limit)
                  << " more triggers)\n";
      }
    }

    if (aux_ok && desc.trigger_aux.count > 0) {
      ctx.Out() << "    Trigger Aux Records:\n";
      const auto aux_count = static_cast<size_t>(desc.trigger_aux.count);
      const auto aux_limit
        = ctx.verbose ? aux_count : (std::min)(aux_count, size_t { 16 });
      for (size_t i = 0; i < aux_limit; ++i) {
        const auto record_offset = static_cast<size_t>(desc.trigger_aux.offset)
          + (i * sizeof(InputTriggerAuxRecord));
        const auto record
          = asset_dump_helpers::ReadRecordAt<InputTriggerAuxRecord>(
            *data, record_offset);
        ctx.Out() << "      [" << i << "] action="
                  << oxygen::data::to_string(record.action_asset_key)
                  << " completion=0x" << std::hex << record.completion_states
                  << std::dec << " time_ns=" << record.time_to_complete_ns
                  << "\n";
      }
      if (aux_count > aux_limit) {
        ctx.Out() << "      ... (" << (aux_count - aux_limit)
                  << " more aux records)\n";
      }
    }

    ctx.Out() << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
