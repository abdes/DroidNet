//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <ostream>
#include <utility>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "DecodedAsset.h"
#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Cooker/Tools/Common/EnumString.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::pakdump {

//! Dumps physics scene asset descriptors.
class PhysicsSceneAssetDumper final : public AssetDumper {
  using RigidBodyBindingRecord
    = oxygen::data::pak::physics::RigidBodyBindingRecord;
  using ColliderBindingRecord
    = oxygen::data::pak::physics::ColliderBindingRecord;
  using CharacterBindingRecord
    = oxygen::data::pak::physics::CharacterBindingRecord;
  using SoftBodyBindingRecord
    = oxygen::data::pak::physics::SoftBodyBindingRecord;
  using JointBindingRecord = oxygen::data::pak::physics::JointBindingRecord;
  using VehicleBindingRecord = oxygen::data::pak::physics::VehicleBindingRecord;
  using AggregateBindingRecord
    = oxygen::data::pak::physics::AggregateBindingRecord;

  static auto PrintSampleRecord(
    std::ostream& out, const RigidBodyBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
    PrintUtils::Field(out, "Sample Shape Key",
      oxygen::data::to_string(record.shape_asset_key), 10);
    PrintUtils::Field(out, "Sample Material Key",
      oxygen::data::to_string(record.material_asset_key), 10);
  }

  static auto PrintSampleRecord(
    std::ostream& out, const ColliderBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
    PrintUtils::Field(out, "Sample Shape Key",
      oxygen::data::to_string(record.shape_asset_key), 10);
    PrintUtils::Field(out, "Sample Is Sensor", record.is_sensor, 10);
  }

  static auto PrintSampleRecord(
    std::ostream& out, const CharacterBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
    PrintUtils::Field(out, "Sample Shape Key",
      oxygen::data::to_string(record.shape_asset_key), 10);
  }

  static auto PrintSampleRecord(
    std::ostream& out, const SoftBodyBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
    PrintUtils::Field(out, "Sample Topology Asset Key",
      oxygen::data::to_string(record.topology_asset_key), 10);
    PrintUtils::Field(out, "Sample Topology Format",
      toolkit::EnumString(record.topology_format), 10);
    PrintUtils::Field(
      out, "Sample Solver Iterations", record.solver_iteration_count, 10);
    PrintUtils::Field(out, "Sample Global Damping", record.global_damping, 10);
    PrintUtils::Field(out, "Sample Restitution", record.restitution, 10);
    PrintUtils::Field(out, "Sample Friction", record.friction, 10);
  }

  static auto PrintSampleRecord(
    std::ostream& out, const JointBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node A", record.node_index_a, 10);
    PrintUtils::Field(out, "Sample Node B", record.node_index_b, 10);
  }

  static auto PrintSampleRecord(
    std::ostream& out, const VehicleBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
    PrintUtils::Field(out, "Sample Controller Type",
      toolkit::EnumString(record.controller_type), 10);
    PrintUtils::Field(
      out, "Sample Wheel Slice Offset", record.wheel_slice_offset, 10);
    PrintUtils::Field(
      out, "Sample Wheel Slice Count", record.wheel_slice_count, 10);
  }

  static auto PrintSampleRecord(
    std::ostream& out, const AggregateBindingRecord& record) -> void
  {
    PrintUtils::Field(out, "Sample Node Index", record.node_index, 10);
    PrintUtils::Field(out, "Sample Max Bodies", record.max_bodies, 10);
  }

  //! Records without a sample of their own print no sample.
  template <typename T>
  static auto PrintSampleRecord(std::ostream& /*out*/, const T& /*record*/)
    -> void
  {
  }

  //! Prints the binding table of type `T` the runtime decoded, if it has one.
  template <typename T>
  static auto PrintTable(std::ostream& out,
    const oxygen::data::PhysicsSceneAsset& scene, const bool verbose,
    uint32_t& table_index, uint64_t& total_bindings) -> void
  {
    const auto bindings = scene.GetBindings<T>();
    if (bindings.empty()) {
      return;
    }

    constexpr auto binding_type = oxygen::data::BindingTraits<T>::kType;
    // A binding type is a FourCC: its value reads best in hexadecimal.
    out << fmt::format("          [{}] {}({:#010X}) count={}\n", table_index,
      nostd::to_string(binding_type), std::to_underlying(binding_type),
      bindings.size());
    ++table_index;
    total_bindings += bindings.size();

    if (verbose) {
      PrintSampleRecord(out, bindings.front());
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
    using oxygen::data::pak::physics::VehicleWheelBindingRecord;

    ctx.Out() << "Asset #" << idx << " (PhysicsScene):\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::TryDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    // The runtime reads it first; a descriptor it refuses is not printed.
    const auto physics_scene
      = DecodeAsset(pak, entry, &loaders::LoadPhysicsSceneAsset);

    const auto scene_read = serio::CopyAt<PhysicsSceneAssetDesc>(*data);
    if (!scene_read) {
      ctx.Out() << "    PhysicsSceneAssetDesc: (insufficient data)\n\n";
      co_return;
    }
    const auto& scene = *scene_read;

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
      ctx.Out(), "Backend", toolkit::EnumString(scene.backend), 8);
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

    ctx.Out() << "        Binding Tables:\n";
    uint32_t table_index = 0;
    uint64_t total_bindings = 0;
    PrintTable<RigidBodyBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<ColliderBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<CharacterBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<SoftBodyBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<JointBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<VehicleBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<VehicleWheelBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintTable<AggregateBindingRecord>(
      ctx.Out(), *physics_scene, ctx.verbose, table_index, total_bindings);
    PrintUtils::Field(ctx.Out(), "Total Bindings", total_bindings, 8);
    ctx.Out() << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
