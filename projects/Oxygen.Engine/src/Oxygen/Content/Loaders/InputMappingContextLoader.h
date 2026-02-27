//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::loaders {

namespace detail {

  inline auto ValidateStringOffset(
    const std::span<const char> string_table, const uint32_t offset) -> void
  {
    if (offset >= string_table.size()) {
      throw std::runtime_error("input mapping slot_name_offset out of bounds");
    }
    const auto begin
      = string_table.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto it = std::find(begin, string_table.end(), '\0');
    if (it == string_table.end()) {
      throw std::runtime_error(
        "input mapping slot name missing null terminator");
    }
  }

  inline auto ValidateTriggerType(const data::pak::input::InputTriggerType type)
    -> void
  {
    using T = data::pak::input::InputTriggerType;
    switch (type) {
    case T::kPressed:
    case T::kReleased:
    case T::kDown:
    case T::kHold:
    case T::kHoldAndRelease:
    case T::kPulse:
    case T::kTap:
    case T::kActionChain:
    case T::kCombo:
      return;
    case T::kChord:
      throw std::runtime_error("input trigger type 'Chord' is not supported");
    default:
      throw std::runtime_error("invalid input trigger type");
    }
  }

} // namespace detail

inline auto LoadInputMappingContextAsset(const LoaderContext& context)
  -> std::unique_ptr<data::InputMappingContextAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto pack = reader.ScopedAlignment(1);
  const auto base_pos_res = reader.Position();
  CheckLoaderResult(
    base_pos_res, "input mapping context asset", "Position(base)");
  const size_t base_pos = *base_pos_res;

  auto desc_blob
    = reader.ReadBlob(sizeof(data::pak::input::InputMappingContextAssetDesc));
  CheckLoaderResult(
    desc_blob, "input mapping context asset", "InputMappingContextAssetDesc");
  data::pak::input::InputMappingContextAssetDesc desc {};
  std::memcpy(&desc, desc_blob->data(), sizeof(desc));

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kInputMappingContext) {
    throw std::runtime_error(
      "invalid asset type for input mapping context descriptor");
  }

  const auto mappings_bytes
    = static_cast<size_t>(desc.mappings.count) * desc.mappings.entry_size;
  const auto triggers_bytes
    = static_cast<size_t>(desc.triggers.count) * desc.triggers.entry_size;
  const auto trigger_aux_bytes
    = static_cast<size_t>(desc.trigger_aux.count) * desc.trigger_aux.entry_size;
  const auto strings_bytes
    = static_cast<size_t>(desc.strings.count) * desc.strings.entry_size;

  if (desc.mappings.count > 0
    && desc.mappings.entry_size
      != sizeof(data::pak::input::InputActionMappingRecord)) {
    throw std::runtime_error("input mappings entry_size mismatch");
  }
  if (desc.triggers.count > 0
    && desc.triggers.entry_size
      != sizeof(data::pak::input::InputTriggerRecord)) {
    throw std::runtime_error("input triggers entry_size mismatch");
  }
  if (desc.trigger_aux.count > 0
    && desc.trigger_aux.entry_size
      != sizeof(data::pak::input::InputTriggerAuxRecord)) {
    throw std::runtime_error("input trigger_aux entry_size mismatch");
  }
  if (desc.strings.count > 0 && desc.strings.entry_size != sizeof(char)) {
    throw std::runtime_error("input strings entry_size mismatch");
  }

  size_t end = sizeof(data::pak::input::InputMappingContextAssetDesc);
  AddRangeEnd(end, desc.mappings.offset, mappings_bytes);
  AddRangeEnd(end, desc.triggers.offset, triggers_bytes);
  AddRangeEnd(end, desc.trigger_aux.offset, trigger_aux_bytes);
  AddRangeEnd(end, desc.strings.offset, strings_bytes);

  CheckLoaderResult(
    reader.Seek(base_pos), "input mapping context asset", "Seek(base)");
  auto bytes_res = reader.ReadBlob(end);
  CheckLoaderResult(bytes_res, "input mapping context asset",
    "ReadBlob(input_mapping_context_payload)");
  auto bytes = std::move(*bytes_res);
  const auto payload = std::span<const std::byte>(bytes);

  std::vector<data::pak::input::InputActionMappingRecord> mappings(
    desc.mappings.count);
  for (uint32_t i = 0; i < desc.mappings.count; ++i) {
    std::memcpy(&mappings[i],
      payload
        .subspan(desc.mappings.offset
            + (static_cast<size_t>(i)
              * sizeof(data::pak::input::InputActionMappingRecord)),
          sizeof(data::pak::input::InputActionMappingRecord))
        .data(),
      sizeof(data::pak::input::InputActionMappingRecord));
  }

  std::vector<data::pak::input::InputTriggerRecord> triggers(
    desc.triggers.count);
  for (uint32_t i = 0; i < desc.triggers.count; ++i) {
    std::memcpy(&triggers[i],
      payload
        .subspan(desc.triggers.offset
            + (static_cast<size_t>(i)
              * sizeof(data::pak::input::InputTriggerRecord)),
          sizeof(data::pak::input::InputTriggerRecord))
        .data(),
      sizeof(data::pak::input::InputTriggerRecord));
    detail::ValidateTriggerType(triggers[i].type);
  }

  std::vector<data::pak::input::InputTriggerAuxRecord> trigger_aux(
    desc.trigger_aux.count);
  for (uint32_t i = 0; i < desc.trigger_aux.count; ++i) {
    std::memcpy(&trigger_aux[i],
      payload
        .subspan(desc.trigger_aux.offset
            + (static_cast<size_t>(i)
              * sizeof(data::pak::input::InputTriggerAuxRecord)),
          sizeof(data::pak::input::InputTriggerAuxRecord))
        .data(),
      sizeof(data::pak::input::InputTriggerAuxRecord));
  }

  std::vector<char> strings(strings_bytes, '\0');
  if (strings_bytes > 0) {
    std::memcpy(strings.data(),
      payload.subspan(desc.strings.offset, strings_bytes).data(),
      strings_bytes);
  }
  const auto string_table
    = std::span<const char>(strings.data(), strings.size());

  for (const auto& mapping : mappings) {
    detail::ValidateStringOffset(string_table, mapping.slot_name_offset);
    const auto trigger_end
      = mapping.trigger_start_index + mapping.trigger_count;
    if (trigger_end < mapping.trigger_start_index
      || trigger_end > triggers.size()) {
      throw std::runtime_error("input mapping trigger range out of bounds");
    }
  }

  for (const auto& trigger : triggers) {
    if (trigger.type == data::pak::input::InputTriggerType::kCombo) {
      const auto aux_end = trigger.aux_start_index + trigger.aux_count;
      if (aux_end < trigger.aux_start_index || aux_end > trigger_aux.size()) {
        throw std::runtime_error("input combo trigger aux range out of bounds");
      }
    }
  }

  if (!context.parse_only) {
    if (!context.dependency_collector) {
      throw std::runtime_error(
        "InputMappingContext loader requires dependency collector "
        "for non-parse-only loads");
    }

    for (const auto& mapping : mappings) {
      if (mapping.action_asset_key != data::AssetKey {}) {
        context.dependency_collector->AddAssetDependency(
          mapping.action_asset_key);
      }
    }
    for (const auto& trigger : triggers) {
      if (trigger.linked_action_asset_key != data::AssetKey {}) {
        context.dependency_collector->AddAssetDependency(
          trigger.linked_action_asset_key);
      }
    }
    for (const auto& aux : trigger_aux) {
      if (aux.action_asset_key != data::AssetKey {}) {
        context.dependency_collector->AddAssetDependency(aux.action_asset_key);
      }
    }
  }

  return std::make_unique<data::InputMappingContextAsset>(
    context.current_asset_key, desc, std::move(mappings), std::move(triggers),
    std::move(trigger_aux), std::move(strings));
}

static_assert(
  oxygen::content::LoadFunction<decltype(LoadInputMappingContextAsset)>);

} // namespace oxygen::content::loaders
