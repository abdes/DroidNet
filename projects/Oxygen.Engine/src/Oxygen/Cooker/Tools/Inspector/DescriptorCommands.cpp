//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <ios>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/DescriptorCommands.h>
#include <Oxygen/Cooker/Tools/Inspector/DumpFormat.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {

namespace {

  auto ReadTableString(const oxygen::data::DescriptorStrings& strings,
    const oxygen::data::pak::core::StringTableOffsetT offset) -> std::string
  {
    const auto value = strings.TryGet(offset);
    return value ? std::string { *value } : std::string { "<invalid>" };
  }

  //! Resolves a string of the descriptor `bytes` start with.
  auto ReadDescriptorString(const std::span<const std::byte> bytes,
    const oxygen::data::pak::core::AssetHeader& header,
    const oxygen::data::pak::core::StringTableOffsetT offset) -> std::string
  {
    try {
      return ReadTableString(
        oxygen::data::DescriptorStrings::FromDescriptor(bytes, header.strings),
        offset);
    } catch (const std::invalid_argument&) {
      return "<invalid string table>";
    }
  }

  auto FormatScriptParamValue(
    const oxygen::data::pak::scripting::ScriptParamRecord& record,
    const oxygen::data::DescriptorStrings& strings) -> std::string
  {
    using oxygen::data::pak::scripting::ScriptParamType;
    switch (record.type) {
    case ScriptParamType::kBool:
      return record.value.as_bool ? "true" : "false";
    case ScriptParamType::kInt32:
      return std::to_string(record.value.as_int32);
    case ScriptParamType::kFloat:
      return std::to_string(record.value.as_float);
    case ScriptParamType::kString:
      return "\"" + ReadTableString(strings, record.value.as_string_offset)
        + "\"";
    case ScriptParamType::kVec2:
      return fmt::format(
        "({}, {})", record.value.as_vec[0], record.value.as_vec[1]);
    case ScriptParamType::kVec3:
      return fmt::format("({}, {}, {})", record.value.as_vec[0],
        record.value.as_vec[1], record.value.as_vec[2]);
    case ScriptParamType::kVec4:
      return fmt::format("({}, {}, {}, {})", record.value.as_vec[0],
        record.value.as_vec[1], record.value.as_vec[2], record.value.as_vec[3]);
    case ScriptParamType::kAsset:
      return fmt::format("{} ({})",
        oxygen::data::to_string(record.value.as_asset.key),
        oxygen::data::to_string(record.value.as_asset.asset_type));
    case ScriptParamType::kNode:
      if (record.value.as_node.node_index
        == oxygen::data::pak::scripting::kUnsetNodeParam) {
        return std::string { "<unset node>" };
      }
      return record.value.as_node.node_path
          == oxygen::data::pak::world::kNoNodePath
        ? fmt::format("node {}", record.value.as_node.node_index)
        : fmt::format("node {} path {}", record.value.as_node.node_index,
            record.value.as_node.node_path);
    case ScriptParamType::kNone:
    default:
      return "<none>";
    }
  }

  auto ReadDescriptorBytes(const std::filesystem::path& descriptor_path)
    -> std::vector<std::byte>
  {
    using oxygen::serio::FileStream;
    using oxygen::serio::Reader;

    FileStream<> stream(descriptor_path, std::ios::in);
    const auto size_result = stream.Size();
    if (!size_result) {
      throw std::runtime_error("failed to query descriptor size");
    }
    const auto size_bytes = size_result.value();
    if (size_bytes == 0) {
      return {};
    }

    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto blob = reader.ReadBlob(size_bytes);
    if (!blob) {
      throw std::runtime_error("failed to read descriptor");
    }
    return *blob;
  }

  template <typename T>
  auto ReadStructAt(const std::vector<std::byte>& bytes, const size_t offset)
    -> std::optional<T>
  {
    if (offset > bytes.size() || sizeof(T) > (bytes.size() - offset)) {
      return std::nullopt;
    }
    T value {};
    std::memcpy(&value, std::span(bytes).subspan(offset).data(), sizeof(T));
    return value;
  }

  auto RunDumpInputActions(std::ostream& out,
    const std::string& cooked_root_path) -> toolkit::ToolResult
  {
    using oxygen::data::AssetType;
    using oxygen::data::pak::input::InputActionAssetDesc;

    const std::filesystem::path cooked_root(cooked_root_path);
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    size_t count = 0;
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type != static_cast<uint8_t>(AssetType::kInputAction)) {
        continue;
      }

      ++count;
      out << "InputAction key='" << oxygen::data::to_string(asset.key)
          << "' desc='" << asset.descriptor_relpath << "'\n";

      const auto descriptor = ReadDescriptorBytes(
        cooked_root / std::filesystem::path(asset.descriptor_relpath));
      if (descriptor.size() < sizeof(InputActionAssetDesc)) {
        out << "  ! descriptor too small\n";
        continue;
      }

      const auto desc = ReadStructAt<InputActionAssetDesc>(descriptor, 0);
      if (!desc) {
        out << "  ! failed to decode descriptor\n";
        continue;
      }

      out << "  name='"
          << ReadDescriptorString(
               descriptor, desc->header, desc->header.name_offset)
          << "' value_type=" << static_cast<uint32_t>(desc->value_type)
          << " flags=" << nostd::to_string(desc->flags) << "\n";
    }

    if (count == 0) {
      out << "(no input actions)\n";
    }
    return {};
  }

  auto RunDumpInputMappings(std::ostream& out,
    const std::string& cooked_root_path) -> toolkit::ToolResult
  {
    using oxygen::data::AssetType;
    using oxygen::data::pak::input::InputActionMappingRecord;
    using oxygen::data::pak::input::InputMappingContextAssetDesc;
    using oxygen::data::pak::input::InputTriggerAuxRecord;
    using oxygen::data::pak::input::InputTriggerRecord;

    const std::filesystem::path cooked_root(cooked_root_path);
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    size_t count = 0;
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type
        != static_cast<uint8_t>(AssetType::kInputMappingContext)) {
        continue;
      }
      ++count;

      out << "InputMappingContext key='" << oxygen::data::to_string(asset.key)
          << "' desc='" << asset.descriptor_relpath << "'\n";

      const auto descriptor = ReadDescriptorBytes(
        cooked_root / std::filesystem::path(asset.descriptor_relpath));
      if (descriptor.size() < sizeof(InputMappingContextAssetDesc)) {
        out << "  ! descriptor too small\n";
        continue;
      }

      const auto desc
        = ReadStructAt<InputMappingContextAssetDesc>(descriptor, 0);
      if (!desc) {
        out << "  ! failed to decode descriptor\n";
        continue;
      }

      out << "  name='"
          << ReadDescriptorString(
               descriptor, desc->header, desc->header.name_offset)
          << "' flags=" << nostd::to_string(desc->flags)
          << " mappings=" << desc->mappings.count
          << " triggers=" << desc->triggers.count
          << " aux=" << desc->trigger_aux.count
          << " strings=" << desc->header.strings.size << "\n";

      const auto mapping_count = static_cast<size_t>(desc->mappings.count);
      const auto mapping_start = static_cast<size_t>(desc->mappings.offset);
      const auto mapping_size = sizeof(InputActionMappingRecord);
      for (size_t i = 0; i < mapping_count; ++i) {
        const auto rec = ReadStructAt<InputActionMappingRecord>(
          descriptor, mapping_start + (i * mapping_size));
        if (!rec) {
          out << "    ! mapping[" << i << "] decode failed\n";
          continue;
        }

        out << "    mapping[" << i
            << "] action=" << oxygen::data::to_string(rec->action_asset_key)
            << " slot='"
            << ReadDescriptorString(
                 descriptor, desc->header, rec->slot_name_offset)
            << "' triggers=[" << rec->trigger_start_index << ","
            << (rec->trigger_start_index + rec->trigger_count)
            << ") flags=" << nostd::to_string(rec->flags) << "\n";
      }

      const auto trigger_count = static_cast<size_t>(desc->triggers.count);
      const auto trigger_start = static_cast<size_t>(desc->triggers.offset);
      const auto trigger_size = sizeof(InputTriggerRecord);
      for (size_t i = 0; i < trigger_count; ++i) {
        const auto rec = ReadStructAt<InputTriggerRecord>(
          descriptor, trigger_start + (i * trigger_size));
        if (!rec) {
          out << "    ! trigger[" << i << "] decode failed\n";
          continue;
        }
        out << "    trigger[" << i << "] type=" << nostd::to_string(rec->type)
            << " behavior=" << nostd::to_string(rec->behavior)
            << " threshold=" << rec->actuation_threshold << " aux=["
            << rec->aux_start_index << ","
            << (rec->aux_start_index + rec->aux_count) << ")\n";
      }

      const auto aux_count = static_cast<size_t>(desc->trigger_aux.count);
      const auto aux_start = static_cast<size_t>(desc->trigger_aux.offset);
      const auto aux_size = sizeof(InputTriggerAuxRecord);
      for (size_t i = 0; i < aux_count; ++i) {
        const auto rec = ReadStructAt<InputTriggerAuxRecord>(
          descriptor, aux_start + (i * aux_size));
        if (!rec) {
          out << "    ! trigger_aux[" << i << "] decode failed\n";
          continue;
        }
        out << "    trigger_aux[" << i
            << "] action=" << oxygen::data::to_string(rec->action_asset_key)
            << " completion=0x" << std::hex << rec->completion_states
            << std::dec << " time_ns=" << rec->time_to_complete_ns << "\n";
      }
    }

    if (count == 0) {
      out << "(no input mapping contexts)\n";
    }
    return {};
  }

  auto RunDumpPhysicsAssets(std::ostream& out,
    const std::string& cooked_root_path) -> toolkit::ToolResult
  {
    using oxygen::data::AssetType;
    using oxygen::data::pak::physics::CollisionShapeAssetDesc;
    using oxygen::data::pak::physics::PhysicsComponentTableDesc;
    using oxygen::data::pak::physics::PhysicsMaterialAssetDesc;
    using oxygen::data::pak::physics::PhysicsSceneAssetDesc;

    const std::filesystem::path cooked_root(cooked_root_path);
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    size_t count = 0;
    for (const auto& asset : inspection.Assets()) {
      const auto type = static_cast<AssetType>(asset.asset_type);
      if (type != AssetType::kPhysicsMaterial
        && type != AssetType::kCollisionShape
        && type != AssetType::kPhysicsScene) {
        continue;
      }
      ++count;

      const auto descriptor = ReadDescriptorBytes(
        cooked_root / std::filesystem::path(asset.descriptor_relpath));

      if (type == AssetType::kPhysicsMaterial) {
        out << "PhysicsMaterial key='" << oxygen::data::to_string(asset.key)
            << "' desc='" << asset.descriptor_relpath << "'\n";
        if (descriptor.size() < sizeof(PhysicsMaterialAssetDesc)) {
          out << "  ! descriptor too small\n";
          continue;
        }
        const auto desc = ReadStructAt<PhysicsMaterialAssetDesc>(descriptor, 0);
        if (!desc) {
          out << "  ! failed to decode descriptor\n";
          continue;
        }
        out << "  name='"
            << ReadDescriptorString(
                 descriptor, desc->header, desc->header.name_offset)
            << "' static_friction=" << desc->static_friction
            << " dynamic_friction=" << desc->dynamic_friction
            << " restitution=" << desc->restitution
            << " density=" << desc->density << "\n";
        continue;
      }

      if (type == AssetType::kCollisionShape) {
        out << "CollisionShape key='" << oxygen::data::to_string(asset.key)
            << "' desc='" << asset.descriptor_relpath << "'\n";
        if (descriptor.size() < sizeof(CollisionShapeAssetDesc)) {
          out << "  ! descriptor too small\n";
          continue;
        }
        const auto desc = ReadStructAt<CollisionShapeAssetDesc>(descriptor, 0);
        if (!desc) {
          out << "  ! failed to decode descriptor\n";
          continue;
        }
        out << "  name='"
            << ReadDescriptorString(
                 descriptor, desc->header, desc->header.name_offset)
            << "' shape_type=" << static_cast<uint32_t>(desc->shape_type)
            << " local_pos=(" << desc->local_position[0] << ","
            << desc->local_position[1] << "," << desc->local_position[2]
            << ") local_scale=(" << desc->local_scale[0] << ","
            << desc->local_scale[1] << "," << desc->local_scale[2]
            << ") is_sensor=" << desc->is_sensor << " material_asset_key="
            << oxygen::data::to_string(desc->material_asset_key)
            << " cooked_ref.asset_key="
            << oxygen::data::to_string(desc->cooked_shape_ref.payload_asset_key)
            << " cooked_ref.type="
            << static_cast<uint32_t>(desc->cooked_shape_ref.payload_type)
            << "\n";
        continue;
      }

      out << "PhysicsScene key='" << oxygen::data::to_string(asset.key)
          << "' desc='" << asset.descriptor_relpath << "'\n";
      if (descriptor.size() < sizeof(PhysicsSceneAssetDesc)) {
        out << "  ! descriptor too small\n";
        continue;
      }
      const auto desc = ReadStructAt<PhysicsSceneAssetDesc>(descriptor, 0);
      if (!desc) {
        out << "  ! failed to decode descriptor\n";
        continue;
      }
      out << "  name='"
          << ReadDescriptorString(
               descriptor, desc->header, desc->header.name_offset)
          << "' target_scene_key="
          << oxygen::data::to_string(desc->target_scene_key)
          << " target_node_count=" << desc->target_node_count
          << " table_count=" << desc->component_table_count
          << " table_dir_offset="
          << ToHex64(desc->component_table_directory_offset) << "\n";

      const auto dir_offset
        = static_cast<size_t>(desc->component_table_directory_offset);
      const auto dir_count = static_cast<size_t>(desc->component_table_count);
      const size_t dir_bytes = dir_count * sizeof(PhysicsComponentTableDesc);
      if (dir_count == 0) {
        continue;
      }
      if (dir_offset > descriptor.size()
        || dir_bytes > (descriptor.size() - dir_offset)) {
        out << "  ! component table directory out of bounds\n";
        continue;
      }

      uint64_t total_bindings = 0;
      for (size_t i = 0; i < dir_count; ++i) {
        const auto entry = ReadStructAt<PhysicsComponentTableDesc>(
          descriptor, dir_offset + (i * sizeof(PhysicsComponentTableDesc)));
        if (!entry) {
          out << "    ! table[" << i << "] decode failed\n";
          continue;
        }
        total_bindings += entry->table.count;
        out << "    table[" << i << "] type=0x" << std::hex
            << static_cast<uint32_t>(entry->binding_type) << std::dec
            << " count=" << entry->table.count
            << " entry_size=" << entry->table.entry_size
            << " offset=" << ToHex64(entry->table.offset) << "\n";
      }
      out << "    total_bindings=" << total_bindings << "\n";
    }

    if (count == 0) {
      out << "(no physics assets)\n";
    }
    return {};
  }

  enum class ScriptDumpMode : uint8_t { kSlots, kParameters };

  auto RunDumpSceneScripts(std::ostream& out,
    const std::string& cooked_root_path, const ScriptDumpMode mode)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(cooked_root_path);
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type
        != static_cast<uint8_t>(oxygen::data::AssetType::kScene)) {
        continue;
      }
      const auto bytes
        = ReadDescriptorBytes(cooked_root / asset.descriptor_relpath);
      const auto scene = oxygen::data::SceneAsset(asset.key, bytes);
      out << "Scene: " << asset.virtual_path << "\n";
      for (const auto& component : scene.GetComponents<
             oxygen::data::pak::scripting::ScriptingComponentRecord>()) {
        const auto slots = scene.ReadScriptSlots(
          component.slot_start_index, component.slot_count);
        for (size_t i = 0; i < slots.size(); ++i) {
          const auto& slot = slots.at(i);
          out << "  Node[" << component.node_index << "] Slot["
              << component.slot_start_index + i
              << "] key=" << oxygen::data::to_string(slot.script_asset_key)
              << " parameters=" << slot.params_count
              << " execution_order=" << slot.execution_order
              << " flags=" << static_cast<uint32_t>(slot.flags) << "\n";
          if (mode == ScriptDumpMode::kParameters) {
            for (const auto& parameter : scene.ReadScriptParameters(slot)) {
              out << "    "
                  << ReadTableString(scene.GetStrings(), parameter.key_offset)
                  << " = "
                  << FormatScriptParamValue(parameter, scene.GetStrings())
                  << "\n";
            }
          }
        }
      }
    }
    return {};
  }

  auto DumpScriptSlots(std::ostream& out, const std::string& cooked_root_path)
    -> toolkit::ToolResult
  {
    return RunDumpSceneScripts(out, cooked_root_path, ScriptDumpMode::kSlots);
  }

  auto DumpScriptParameters(std::ostream& out,
    const std::string& cooked_root_path) -> toolkit::ToolResult
  {
    return RunDumpSceneScripts(
      out, cooked_root_path, ScriptDumpMode::kParameters);
  }

} // namespace

auto MakeInputActionsCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "input-actions",
    .about = "Dump input action descriptors.",
    .dump = RunDumpInputActions,
  });
}

auto MakeInputMappingsCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "input-mappings",
    .about = "Dump input mapping context descriptors.",
    .dump = RunDumpInputMappings,
  });
}

auto MakePhysicsAssetsCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "physics",
    .about = "Dump physics asset descriptors.",
    .dump = RunDumpPhysicsAssets,
  });
}

auto MakeScriptSlotsCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "script-slots",
    .about = "Dump scene-local script slots, grouped by scene and node.",
    .dump = DumpScriptSlots,
  });
}

auto MakeScriptParametersCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "script-params",
    .about = "Dump scene-local script slots and their parameter values.",
    .dump = DumpScriptParameters,
  });
}

} // namespace oxygen::content::inspection
