//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstring>
#include <ostream>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include <fmt/format.h>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::pakdump {

//! Dumps physics scene asset descriptors.
class PhysicsSceneAssetDumper final : public AssetDumper {
  static auto BindingTypeName(
    const oxygen::data::pak::physics::PhysicsBindingType type) -> const char*
  {
    using oxygen::data::pak::physics::PhysicsBindingType;
    switch (type) {
    case PhysicsBindingType::kRigidBody:
      return "RigidBody";
    case PhysicsBindingType::kCollider:
      return "Collider";
    case PhysicsBindingType::kCharacter:
      return "Character";
    case PhysicsBindingType::kSoftBody:
      return "SoftBody";
    case PhysicsBindingType::kJoint:
      return "Joint";
    case PhysicsBindingType::kVehicle:
      return "Vehicle";
    case PhysicsBindingType::kAggregate:
      return "Aggregate";
    default:
      return "Unknown";
    }
  }

  static auto InRange(
    const size_t offset, const size_t size, const size_t total) -> bool
  {
    return offset <= total && size <= (total - offset);
  }

  static auto PrintSampleRecord(std::ostream& out,
    const std::vector<std::byte>& blob, const size_t table_offset,
    const oxygen::data::pak::physics::PhysicsBindingType type) -> void
  {
    using oxygen::data::pak::physics::AggregateBindingRecord;
    using oxygen::data::pak::physics::CharacterBindingRecord;
    using oxygen::data::pak::physics::ColliderBindingRecord;
    using oxygen::data::pak::physics::JointBindingRecord;
    using oxygen::data::pak::physics::PhysicsBindingType;
    using oxygen::data::pak::physics::RigidBodyBindingRecord;
    using oxygen::data::pak::physics::SoftBodyBindingRecord;
    using oxygen::data::pak::physics::VehicleBindingRecord;

    switch (type) {
    case PhysicsBindingType::kRigidBody: {
      const auto record
        = asset_dump_helpers::ReadRecordAt<RigidBodyBindingRecord>(
          blob, table_offset);
      PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
      PrintUtils::Field(out, "Sample Shape Key",
        oxygen::data::to_string(record.shape_asset_key), 10);
      PrintUtils::Field(out, "Sample Material Key",
        oxygen::data::to_string(record.material_asset_key), 10);
      break;
    }
    case PhysicsBindingType::kCollider: {
      const auto record
        = asset_dump_helpers::ReadRecordAt<ColliderBindingRecord>(
          blob, table_offset);
      PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
      PrintUtils::Field(out, "Sample Shape Key",
        oxygen::data::to_string(record.shape_asset_key), 10);
      PrintUtils::Field(out, "Sample Is Sensor", record.is_sensor, 10);
      break;
    }
    case PhysicsBindingType::kCharacter: {
      const auto record
        = asset_dump_helpers::ReadRecordAt<CharacterBindingRecord>(
          blob, table_offset);
      PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
      PrintUtils::Field(out, "Sample Shape Key",
        oxygen::data::to_string(record.shape_asset_key), 10);
      break;
    }
    case PhysicsBindingType::kSoftBody: {
      const auto record
        = asset_dump_helpers::ReadRecordAt<SoftBodyBindingRecord>(
          blob, table_offset);
      PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
      PrintUtils::Field(out, "Sample Topology Asset Key",
        oxygen::data::to_string(record.topology_asset_key), 10);
      PrintUtils::Field(out, "Sample Topology Format",
        static_cast<uint32_t>(record.topology_format), 10);
      PrintUtils::Field(
        out, "Sample Solver Iterations", record.solver_iteration_count, 10);
      PrintUtils::Field(
        out, "Sample Global Damping", record.global_damping, 10);
      PrintUtils::Field(out, "Sample Restitution", record.restitution, 10);
      PrintUtils::Field(out, "Sample Friction", record.friction, 10);
      break;
    }
    case PhysicsBindingType::kJoint: {
      const auto record = asset_dump_helpers::ReadRecordAt<JointBindingRecord>(
        blob, table_offset);
      PrintUtils::Field(out, "Sample Node A", record.node_index_a, 10);
      PrintUtils::Field(out, "Sample Node B", record.node_index_b, 10);
      break;
    }
    case PhysicsBindingType::kVehicle: {
      const auto record
        = asset_dump_helpers::ReadRecordAt<VehicleBindingRecord>(
          blob, table_offset);
      PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
      PrintUtils::Field(out, "Sample Controller Type",
        static_cast<uint32_t>(record.controller_type), 10);
      PrintUtils::Field(
        out, "Sample Wheel Slice Offset", record.wheel_slice_offset, 10);
      PrintUtils::Field(
        out, "Sample Wheel Slice Count", record.wheel_slice_count, 10);
      break;
    }
    case PhysicsBindingType::kAggregate: {
      const auto record
        = asset_dump_helpers::ReadRecordAt<AggregateBindingRecord>(
          blob, table_offset);
      PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
      PrintUtils::Field(out, "Sample Max Bodies", record.max_bodies, 10);
      break;
    }
    default:
      break;
    }
  }

public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::physics::PhysicsSceneAssetDesc;

    ctx.Out() << "Asset #" << idx << " (PhysicsScene):\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    if (data->size() < sizeof(PhysicsSceneAssetDesc)) {
      ctx.Out() << "    PhysicsSceneAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    PhysicsSceneAssetDesc scene {};
    std::memcpy(&scene, data->data(), sizeof(scene));

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), scene.header,
      asset_dump_helpers::DescriptorString(
        *data, scene.header, scene.header.name_offset),
      4);

    ctx.Out() << "    --- Physics Scene Sidecar Fields ---\n";
    PrintUtils::Field(ctx.Out(), "Target Scene Key",
      oxygen::data::to_string(scene.target_scene_key), 8);
    PrintUtils::Field(
      ctx.Out(), "Target Node Count", scene.target_node_count, 8);
    PrintUtils::Field(
      ctx.Out(), "Component Table Count", scene.component_table_count, 8);
    PrintUtils::Field(ctx.Out(), "Component Directory Offset",
      asset_dump_helpers::ToHexString(scene.component_table_directory_offset),
      8);

    if (scene.component_table_count == 0) {
      PrintUtils::Field(ctx.Out(), "Binding Tables", "none", 8);
      ctx.Out() << "\n";
      co_return;
    }

    const auto dir_offset
      = static_cast<size_t>(scene.component_table_directory_offset);
    const size_t dir_size = static_cast<size_t>(scene.component_table_count)
      * sizeof(oxygen::data::pak::physics::PhysicsComponentTableDesc);
    if (!InRange(dir_offset, dir_size, data->size())) {
      PrintUtils::Field(
        ctx.Out(), "Binding Tables", "directory out of bounds", 8);
      ctx.Out() << "\n";
      co_return;
    }

    ctx.Out() << "        Binding Tables:\n";
    uint64_t total_bindings = 0;
    for (uint32_t i = 0; i < scene.component_table_count; ++i) {
      const size_t entry_offset = dir_offset
        + (static_cast<size_t>(i)
          * sizeof(oxygen::data::pak::physics::PhysicsComponentTableDesc));
      const auto table_desc = asset_dump_helpers::ReadRecordAt<
        oxygen::data::pak::physics::PhysicsComponentTableDesc>(
        *data, entry_offset);

      const auto table_offset = static_cast<size_t>(table_desc.table.offset);
      const auto count = static_cast<size_t>(table_desc.table.count);
      const auto entry_size = static_cast<size_t>(table_desc.table.entry_size);
      const auto table_size = count * entry_size;

      ctx.Out() << fmt::format(
        "          [{}] {} ({:#010X}) count={} entry_size={} "
        "offset={}\n",
        i, BindingTypeName(table_desc.binding_type),
        static_cast<uint32_t>(table_desc.binding_type), table_desc.table.count,
        table_desc.table.entry_size,
        asset_dump_helpers::ToHexString(table_desc.table.offset));
      total_bindings += table_desc.table.count;

      if (!InRange(table_offset, table_size, data->size())) {
        PrintUtils::Field(ctx.Out(), "Table Status", "out of bounds", 10);
        continue;
      }

      if (ctx.verbose && count > 0) {
        PrintSampleRecord(
          ctx.Out(), *data, table_offset, table_desc.binding_type);
      }
    }
    PrintUtils::Field(ctx.Out(), "Total Bindings", total_bindings, 8);
    ctx.Out() << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
