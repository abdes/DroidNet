//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>

#include <fmt/format.h>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps physics scene asset descriptors.
class PhysicsSceneAssetDumper final : public AssetDumper {
  static auto BindingTypeName(
    const oxygen::data::pak::v7::PhysicsBindingType type) -> const char*
  {
    using oxygen::data::pak::v7::PhysicsBindingType;
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

  static auto PrintSampleRecord(const std::vector<std::byte>& blob,
    const size_t table_offset,
    const oxygen::data::pak::v7::PhysicsBindingType type) -> void
  {
    using namespace oxygen::data::pak::v7;

    switch (type) {
    case PhysicsBindingType::kRigidBody: {
      RigidBodyBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node Index", record.node_index, 10);
      PrintUtils::Field("Sample Shape Index", record.shape_asset_index, 10);
      PrintUtils::Field(
        "Sample Material Index", record.material_asset_index, 10);
      break;
    }
    case PhysicsBindingType::kCollider: {
      ColliderBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node Index", record.node_index, 10);
      PrintUtils::Field("Sample Shape Index", record.shape_asset_index, 10);
      break;
    }
    case PhysicsBindingType::kCharacter: {
      CharacterBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node Index", record.node_index, 10);
      PrintUtils::Field("Sample Shape Index", record.shape_asset_index, 10);
      break;
    }
    case PhysicsBindingType::kSoftBody: {
      SoftBodyBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node Index", record.node_index, 10);
      PrintUtils::Field("Sample Clusters", record.cluster_count, 10);
      break;
    }
    case PhysicsBindingType::kJoint: {
      JointBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node A", record.node_index_a, 10);
      PrintUtils::Field("Sample Node B", record.node_index_b, 10);
      break;
    }
    case PhysicsBindingType::kVehicle: {
      VehicleBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node Index", record.node_index, 10);
      break;
    }
    case PhysicsBindingType::kAggregate: {
      AggregateBindingRecord record {};
      std::memcpy(&record, blob.data() + table_offset, sizeof(record));
      PrintUtils::Field("Sample Node Index", record.node_index, 10);
      PrintUtils::Field("Sample Max Bodies", record.max_bodies, 10);
      break;
    }
    default:
      break;
    }
  }

public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::v7::PhysicsSceneAssetDesc;

    std::cout << "Asset #" << idx << " (PhysicsScene):\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    if (data->size() < sizeof(PhysicsSceneAssetDesc)) {
      std::cout << "    PhysicsSceneAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    PhysicsSceneAssetDesc scene {};
    std::memcpy(&scene, data->data(), sizeof(scene));

    asset_dump_helpers::PrintAssetHeaderFields(scene.header, 4);

    std::cout << "    --- Physics Scene Sidecar Fields ---\n";
    PrintUtils::Field(
      "Target Scene Key", oxygen::data::to_string(scene.target_scene_key), 8);
    PrintUtils::Field("Target Node Count", scene.target_node_count, 8);
    PrintUtils::Field("Component Table Count", scene.component_table_count, 8);
    PrintUtils::Field("Component Directory Offset",
      asset_dump_helpers::ToHexString(scene.component_table_directory_offset),
      8);

    if (scene.component_table_count == 0) {
      PrintUtils::Field("Binding Tables", "none", 8);
      std::cout << "\n";
      co_return;
    }

    const size_t dir_offset
      = static_cast<size_t>(scene.component_table_directory_offset);
    const size_t dir_size = static_cast<size_t>(scene.component_table_count)
      * sizeof(oxygen::data::pak::v7::PhysicsComponentTableDesc);
    if (!InRange(dir_offset, dir_size, data->size())) {
      PrintUtils::Field("Binding Tables", "directory out of bounds", 8);
      std::cout << "\n";
      co_return;
    }

    std::cout << "        Binding Tables:\n";
    uint64_t total_bindings = 0;
    for (uint32_t i = 0; i < scene.component_table_count; ++i) {
      const size_t entry_offset = dir_offset
        + static_cast<size_t>(i)
          * sizeof(oxygen::data::pak::v7::PhysicsComponentTableDesc);
      oxygen::data::pak::v7::PhysicsComponentTableDesc table_desc {};
      std::memcpy(&table_desc, data->data() + entry_offset, sizeof(table_desc));

      const auto table_offset = static_cast<size_t>(table_desc.table.offset);
      const auto count = static_cast<size_t>(table_desc.table.count);
      const auto entry_size = static_cast<size_t>(table_desc.table.entry_size);
      const auto table_size = count * entry_size;

      std::cout << fmt::format(
        "          [{}] {} ({:#010X}) count={} entry_size={} "
        "offset={}\n",
        i, BindingTypeName(table_desc.binding_type),
        static_cast<uint32_t>(table_desc.binding_type), table_desc.table.count,
        table_desc.table.entry_size,
        asset_dump_helpers::ToHexString(table_desc.table.offset));
      total_bindings += table_desc.table.count;

      if (!InRange(table_offset, table_size, data->size())) {
        PrintUtils::Field("Table Status", "out of bounds", 10);
        continue;
      }

      if (ctx.verbose && count > 0) {
        PrintSampleRecord(*data, table_offset, table_desc.binding_type);
      }
    }
    PrintUtils::Field("Total Bindings", total_bindings, 8);
    std::cout << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
