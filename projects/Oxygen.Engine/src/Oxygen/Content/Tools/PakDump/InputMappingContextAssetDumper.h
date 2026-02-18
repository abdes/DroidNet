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
#include <iostream>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "PrintUtils.h"

namespace oxygen::content::pakdump {

class InputMappingContextAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;
    using oxygen::data::pak::InputActionMappingRecord;
    using oxygen::data::pak::InputMappingContextAssetDesc;
    using oxygen::data::pak::InputTriggerAuxRecord;
    using oxygen::data::pak::InputTriggerRecord;

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(InputMappingContextAssetDesc)) {
      std::cout << "    InputMappingContextAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    InputMappingContextAssetDesc desc {};
    std::memcpy(&desc, data->data(), sizeof(desc));

    asset_dump_helpers::PrintAssetHeaderFields(desc.header, 4);
    std::cout << "    --- Input Mapping Context Descriptor Fields ---\n";
    PrintUtils::Field("Flags", oxygen::data::pak::to_string(desc.flags), 8);
    PrintUtils::Field("Mappings Count", desc.mappings.count, 8);
    PrintUtils::Field("Mappings Entry Size", desc.mappings.entry_size, 8);
    PrintUtils::Field(
      "Triggers Count", static_cast<uint32_t>(desc.triggers.count), 8);
    PrintUtils::Field("Trigger Aux Count", desc.trigger_aux.count, 8);
    PrintUtils::Field("String Table Bytes", desc.strings.count, 8);

    const auto ParseTable
      = [&](const oxygen::data::pak::InputDataTable& table,
          const size_t expected_size, const char* const table_name) -> bool {
      const auto offset = static_cast<size_t>(table.offset);
      const auto count = static_cast<size_t>(table.count);
      const auto size = static_cast<size_t>(table.entry_size);

      if (count == 0) {
        return true;
      }
      if (size != expected_size) {
        PrintUtils::Field("Warning",
          fmt::format("{} entry size mismatch: {} != {}", table_name, size,
            expected_size),
          8);
        return false;
      }
      if (offset > data->size()) {
        PrintUtils::Field(
          "Warning", fmt::format("{} offset out of bounds", table_name), 8);
        return false;
      }
      const auto total_size = count * expected_size;
      if (total_size > (data->size() - offset)) {
        PrintUtils::Field("Warning",
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
    const auto strings_offset = static_cast<size_t>(desc.strings.offset);
    const auto strings_size = static_cast<size_t>(desc.strings.count);
    const auto strings_ok = strings_size == 0
      || (strings_offset <= data->size() && strings_size <= data->size()
        && strings_offset <= data->size() - strings_size);
    if (!strings_ok) {
      PrintUtils::Field(
        "Warning", "String table payload exceeds descriptor", 8);
    }

    if (!ctx.verbose) {
      std::cout << "\n";
      co_return;
    }

    std::string_view strings {};
    if (strings_ok && strings_size > 0) {
      strings = std::string_view(
        reinterpret_cast<const char*>(data->data() + strings_offset),
        strings_size);
    }

    if (mappings_ok && desc.mappings.count > 0) {
      std::cout << "    Mappings:\n";
      const auto mapping_count = static_cast<size_t>(desc.mappings.count);
      const auto mapping_limit = ctx.verbose
        ? mapping_count
        : (std::min)(mapping_count, size_t { 16 });
      for (size_t i = 0; i < mapping_limit; ++i) {
        InputActionMappingRecord record {};
        const auto record_offset = static_cast<size_t>(desc.mappings.offset)
          + i * sizeof(InputActionMappingRecord);
        std::memcpy(&record, data->data() + record_offset, sizeof(record));

        const auto slot_name = asset_dump_helpers::TryGetSceneString(
          strings, record.slot_name_offset);
        std::cout << "      [" << i << "] action="
                  << oxygen::data::to_string(record.action_asset_key)
                  << " slot='"
                  << (slot_name.empty() ? std::string_view { "<invalid>" }
                                        : slot_name)
                  << "'\n";
        PrintUtils::Field("Trigger Range",
          fmt::format("[{}, {}]", record.trigger_start_index,
            record.trigger_start_index + record.trigger_count),
          10);
        PrintUtils::Field(
          "Flags", oxygen::data::pak::to_string(record.flags), 10);
        PrintUtils::Field("Scale",
          fmt::format("[{:.3f}, {:.3f}]", record.scale[0], record.scale[1]),
          10);
        PrintUtils::Field("Bias",
          fmt::format("[{:.3f}, {:.3f}]", record.bias[0], record.bias[1]), 10);
      }
      if (mapping_count > mapping_limit) {
        std::cout << "      ... (" << (mapping_count - mapping_limit)
                  << " more mappings)\n";
      }
    }

    if (triggers_ok && desc.triggers.count > 0) {
      std::cout << "    Triggers:\n";
      const auto trigger_count = static_cast<size_t>(desc.triggers.count);
      const auto trigger_limit = ctx.verbose
        ? trigger_count
        : (std::min)(trigger_count, size_t { 16 });
      for (size_t i = 0; i < trigger_limit; ++i) {
        InputTriggerRecord record {};
        const auto record_offset = static_cast<size_t>(desc.triggers.offset)
          + i * sizeof(InputTriggerRecord);
        std::memcpy(&record, data->data() + record_offset, sizeof(record));

        std::cout << "      [" << i
                  << "] type=" << oxygen::data::pak::to_string(record.type)
                  << " behavior="
                  << oxygen::data::pak::to_string(record.behavior) << "\n";
        PrintUtils::Field(
          "Actuation Threshold", record.actuation_threshold, 10);
        PrintUtils::Field("Linked Action",
          oxygen::data::to_string(record.linked_action_asset_key), 10);
        PrintUtils::Field("Aux Range",
          fmt::format("[{}, {}]", record.aux_start_index,
            record.aux_start_index + record.aux_count),
          10);
      }
      if (trigger_count > trigger_limit) {
        std::cout << "      ... (" << (trigger_count - trigger_limit)
                  << " more triggers)\n";
      }
    }

    if (aux_ok && desc.trigger_aux.count > 0) {
      std::cout << "    Trigger Aux Records:\n";
      const auto aux_count = static_cast<size_t>(desc.trigger_aux.count);
      const auto aux_limit
        = ctx.verbose ? aux_count : (std::min)(aux_count, size_t { 16 });
      for (size_t i = 0; i < aux_limit; ++i) {
        InputTriggerAuxRecord record {};
        const auto record_offset = static_cast<size_t>(desc.trigger_aux.offset)
          + i * sizeof(InputTriggerAuxRecord);
        std::memcpy(&record, data->data() + record_offset, sizeof(record));
        std::cout << "      [" << i << "] action="
                  << oxygen::data::to_string(record.action_asset_key)
                  << " completion=0x" << std::hex << record.completion_states
                  << std::dec << " time_ns=" << record.time_to_complete_ns
                  << "\n";
      }
      if (aux_count > aux_limit) {
        std::cout << "      ... (" << (aux_count - aux_limit)
                  << " more aux records)\n";
      }
    }

    std::cout << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
