//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::loaders {

namespace detail {

  //! Refuses a trigger of an unknown type or behavior, or a chord or chain
  //! trigger that names no linked action.
  inline auto ValidateTrigger(
    const data::pak::input::InputTriggerRecord& trigger) -> void
  {
    using Type = data::pak::input::InputTriggerType;
    switch (trigger.type) {
    case Type::kChord:
    case Type::kActionChain:
      if (trigger.linked_action_asset_key.IsNil()) {
        throw ContentLoadError(ContentLoadFailure::kContentInvalid,
          "input chord or chain trigger names no linked action");
      }
      break;
    case Type::kPressed:
    case Type::kReleased:
    case Type::kDown:
    case Type::kHold:
    case Type::kHoldAndRelease:
    case Type::kPulse:
    case Type::kTap:
    case Type::kCombo:
      break;
    default:
      throw data::CookedFormatError(
        data::CookedDefect::kInvalidValue, "invalid input trigger type");
    }
    using Behavior = data::pak::input::InputTriggerBehavior;
    switch (trigger.behavior) {
    case Behavior::kExplicit:
    case Behavior::kImplicit:
    case Behavior::kBlocker:
      return;
    default:
      throw data::CookedFormatError(
        data::CookedDefect::kInvalidValue, "invalid input trigger behavior");
    }
  }

} // namespace detail

inline auto LoadInputMappingContextAsset(const LoaderContext& context)
  -> std::unique_ptr<data::InputMappingContextAsset>
{
  LOG_SCOPE_FUNCTION(1);

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
  const auto parsed_desc
    = serio::CopyAt<data::pak::input::InputMappingContextAssetDesc>(*desc_blob);
  CheckLoaderResult(
    parsed_desc, "input mapping context asset", "InputMappingContextAssetDesc");
  const auto desc = *parsed_desc;

  if (desc.header.version
    != data::pak::input::kInputMappingContextAssetVersion) {
    throw data::CookedFormatError(data::CookedDefect::kUnsupportedVersion,
      "Input descriptor version is not current; re-cook the asset");
  }

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kInputMappingContext) {
    throw data::CookedFormatError(data::CookedDefect::kWrongMagic,
      "invalid asset type for input mapping context descriptor");
  }
  if (desc.flags != data::pak::input::InputMappingContextFlags::kNone) {
    throw data::CookedFormatError(data::CookedDefect::kReservedNotZero,
      "input mapping context descriptor sets reserved flags");
  }

  const auto mappings_bytes
    = static_cast<size_t>(desc.mappings.count) * desc.mappings.entry_size;
  const auto triggers_bytes
    = static_cast<size_t>(desc.triggers.count) * desc.triggers.entry_size;
  const auto trigger_aux_bytes
    = static_cast<size_t>(desc.trigger_aux.count) * desc.trigger_aux.entry_size;

  if (desc.mappings.count > 0
    && desc.mappings.entry_size
      != sizeof(data::pak::input::InputActionMappingRecord)) {
    throw data::CookedFormatError(
      data::CookedDefect::kWrongRowSize, "input mappings entry_size mismatch");
  }
  if (desc.triggers.count > 0
    && desc.triggers.entry_size
      != sizeof(data::pak::input::InputTriggerRecord)) {
    throw data::CookedFormatError(
      data::CookedDefect::kWrongRowSize, "input triggers entry_size mismatch");
  }
  if (desc.trigger_aux.count > 0
    && desc.trigger_aux.entry_size
      != sizeof(data::pak::input::InputTriggerAuxRecord)) {
    throw data::CookedFormatError(data::CookedDefect::kWrongRowSize,
      "input trigger_aux entry_size mismatch");
  }
  if (desc.header.strings.size == 0
    || desc.header.strings.offset
      < sizeof(data::pak::input::InputMappingContextAssetDesc)) {
    throw data::CookedFormatError(
      data::CookedDefect::kOutOfBounds, "input strings location is invalid");
  }

  size_t end = sizeof(data::pak::input::InputMappingContextAssetDesc);
  AddRangeEnd(end, desc.mappings.offset, mappings_bytes);
  AddRangeEnd(end, desc.triggers.offset, triggers_bytes);
  AddRangeEnd(end, desc.trigger_aux.offset, trigger_aux_bytes);
  AddRangeEnd(end, desc.header.strings.offset, desc.header.strings.size);

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
    const auto record
      = serio::CopyAt<data::pak::input::InputActionMappingRecord>(payload,
        serio::ByteOffset { desc.mappings.offset
          + (static_cast<size_t>(i)
            * sizeof(data::pak::input::InputActionMappingRecord)) });
    CheckLoaderResult(
      record, "input mapping context asset", "InputActionMappingRecord");
    mappings.at(i) = *record;
  }

  std::vector<data::pak::input::InputTriggerRecord> triggers(
    desc.triggers.count);
  for (uint32_t i = 0; i < desc.triggers.count; ++i) {
    const auto record
      = serio::CopyAt<data::pak::input::InputTriggerRecord>(payload,
        serio::ByteOffset { desc.triggers.offset
          + (static_cast<size_t>(i)
            * sizeof(data::pak::input::InputTriggerRecord)) });
    CheckLoaderResult(
      record, "input mapping context asset", "InputTriggerRecord");
    triggers.at(i) = *record;
    detail::ValidateTrigger(triggers.at(i));
  }

  std::vector<data::pak::input::InputTriggerAuxRecord> trigger_aux(
    desc.trigger_aux.count);
  for (uint32_t i = 0; i < desc.trigger_aux.count; ++i) {
    const auto record
      = serio::CopyAt<data::pak::input::InputTriggerAuxRecord>(payload,
        serio::ByteOffset { desc.trigger_aux.offset
          + (static_cast<size_t>(i)
            * sizeof(data::pak::input::InputTriggerAuxRecord)) });
    CheckLoaderResult(
      record, "input mapping context asset", "InputTriggerAuxRecord");
    trigger_aux.at(i) = *record;
    if (trigger_aux.at(i).action_asset_key.IsNil()) {
      throw ContentLoadError(ContentLoadFailure::kContentInvalid,
        "input combo step names no action");
    }
  }

  auto strings = data::DescriptorStrings {};
  try {
    strings
      = data::DescriptorStrings::FromDescriptor(payload, desc.header.strings);
  } catch (const std::invalid_argument& error) {
    throw data::CookedFormatError(data::CookedDefect::kInvalidString,
      fmt::format("input mapping context strings: {}", error.what()));
  }
  auto name
    = ResolveDescriptorString(strings, desc.header.name_offset, "asset name");

  for (const auto& mapping : mappings) {
    const auto slot = ResolveDescriptorString(
      strings, mapping.slot_name_offset, "input mapping slot name");
    if (!platform::InputSlots::FindByName(slot)) {
      throw ContentLoadError(ContentLoadFailure::kContentInvalid,
        fmt::format("input mapping slot '{}' is unknown", slot));
    }
    if (mapping.action_asset_key.IsNil()) {
      throw ContentLoadError(
        ContentLoadFailure::kContentInvalid, "input mapping names no action");
    }
    const auto trigger_end
      = mapping.trigger_start_index + mapping.trigger_count;
    if (trigger_end < mapping.trigger_start_index
      || trigger_end > triggers.size()) {
      throw data::CookedFormatError(data::CookedDefect::kInvalidReference,
        "input mapping trigger range out of bounds");
    }
  }

  for (const auto& trigger : triggers) {
    if (trigger.type == data::pak::input::InputTriggerType::kCombo) {
      const auto aux_end = trigger.aux_start_index + trigger.aux_count;
      if (aux_end < trigger.aux_start_index || aux_end > trigger_aux.size()) {
        throw data::CookedFormatError(data::CookedDefect::kInvalidReference,
          "input combo trigger aux range out of bounds");
      }
    }
  }

  if (context.asset_references) {
    std::vector<data::KeyReference> references;
    references.reserve(mappings.size() + triggers.size() + trigger_aux.size());
    const auto add_action = [&references](const data::AssetKey& key) -> void {
      if (!key.IsNil()) {
        references.push_back({
          .key = key,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kInputAction,
        });
      }
    };
    for (const auto& mapping : mappings) {
      add_action(mapping.action_asset_key);
    }
    for (const auto& trigger : triggers) {
      add_action(trigger.linked_action_asset_key);
    }
    for (const auto& auxiliary : trigger_aux) {
      add_action(auxiliary.action_asset_key);
    }
    context.ValidateReferences({}, references);
  }

  if (!context.parse_only) {
    DCHECK_NOTNULL_F(context.dependency_collector,
      "an input mapping context decode collects its actions");

    for (const auto& mapping : mappings) {
      context.dependency_collector->AddAssetDependency(
        mapping.action_asset_key);
    }
    for (const auto& trigger : triggers) {
      if (!trigger.linked_action_asset_key.IsNil()) {
        context.dependency_collector->AddAssetDependency(
          trigger.linked_action_asset_key);
      }
    }
    for (const auto& aux : trigger_aux) {
      context.dependency_collector->AddAssetDependency(aux.action_asset_key);
    }
  }

  auto asset = std::make_unique<data::InputMappingContextAsset>(
    context.current_asset_key, desc, std::move(mappings), std::move(triggers),
    std::move(trigger_aux), std::move(strings),
    data::SourceOrigin {
      .key = context.source_key,
      .instance = context.source_instance,
    });
  asset->SetAssetName(std::move(name));
  return asset;
}

static_assert(
  oxygen::content::LoadFunction<decltype(LoadInputMappingContextAsset)>);

} // namespace oxygen::content::loaders
