//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::data {

namespace {

  namespace physics = pak::physics;

  //! Refuses the descriptor for breaking the rule `rule` describes.
  [[noreturn]] auto Refuse(
    const CookedDefect defect, const std::string_view rule) -> void
  {
    throw CookedOpenError(defect, "PhysicsSceneAsset " + std::string(rule));
  }

  //! Whether `size` bytes at `offset` lie within `total` bytes.
  auto RangeFits(const std::size_t offset, const std::size_t size,
    const std::size_t total) noexcept -> bool
  {
    return offset <= total && size <= total - offset;
  }

  //! Whether `type` is the type of a binding table the format defines.
  constexpr auto DefinesBindingType(
    const physics::PhysicsBindingType type) noexcept -> bool
  {
    return [type]<typename... Record>(
             std::type_identity<std::tuple<Record...>>) -> bool {
      return ((type == BindingTraits<Record>::kType) || ...);
    }(std::type_identity<PhysicsBindingRecords> {});
  }

  //! The packed record `T` at `offset` of `bytes`, which holds it.
  template <typename T>
  auto RecordAt(const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept -> T
  {
    T record {};
    std::memcpy(&record, bytes.subspan(offset, sizeof(T)).data(), sizeof(T));
    return record;
  }

  //! The records of the binding table `table` of fixed-size `T` records.
  template <typename T>
  auto ReadFixedTable(const std::span<const std::byte> bytes,
    const pak::core::ResourceTable& table) -> std::vector<T>
  {
    const auto size = std::size_t { table.count } * sizeof(T);
    if (!RangeFits(table.offset, size, bytes.size())) {
      Refuse(CookedDefect::kOutOfBounds,
        "binding table lies outside the descriptor");
    }
    std::vector<T> records;
    records.reserve(table.count);
    for (auto index = uint32_t { 0U }; index < table.count; ++index) {
      records.push_back(
        RecordAt<T>(bytes, table.offset + (std::size_t { index } * sizeof(T))));
    }
    return records;
  }

  //! The count and self-relative offset of a vertex list a soft body record
  //! names.
  struct VertexListField {
    uint32_t count = 0;
    uint32_t byte_offset = 0;
  };

  //! Reads the vertex list `field` of the soft body record `unit` starts
  //! with; it starts where the record's unit ends so far, `end`, which then
  //! moves past it.
  auto ReadVertexList(const std::span<const std::byte> unit, std::size_t& end,
    const VertexListField field) -> std::vector<uint32_t>
  {
    if (field.count == 0U) {
      if (field.byte_offset != 0U) {
        Refuse(CookedDefect::kNotCanonical,
          "empty soft body vertex list has an offset");
      }
      return {};
    }
    if (field.byte_offset != end) {
      Refuse(CookedDefect::kNotCanonical,
        "soft body vertex list does not follow its record");
    }
    const auto size = std::size_t { field.count } * sizeof(uint32_t);
    if (!RangeFits(end, size, unit.size())) {
      Refuse(CookedDefect::kOutOfBounds,
        "soft body vertex list lies outside the descriptor");
    }
    std::vector<uint32_t> list(field.count);
    std::memcpy(list.data(), unit.subspan(end, size).data(), size);
    end += size;
    return list;
  }

  //! Reads the soft bodies of `table`, each a record followed by its pinned
  //! and then its kinematic vertex list.
  auto ReadSoftBodies(const std::span<const std::byte> bytes,
    const pak::core::ResourceTable& table,
    std::vector<physics::SoftBodyBindingRecord>& records,
    std::vector<SoftBodyVertexLists>& vertices) -> void
  {
    using physics::SoftBodyBindingRecord;
    auto at = std::size_t { table.offset };
    for (auto index = uint32_t { 0U }; index < table.count; ++index) {
      if (!RangeFits(at, sizeof(SoftBodyBindingRecord), bytes.size())) {
        Refuse(CookedDefect::kOutOfBounds,
          "binding table lies outside the descriptor");
      }
      const auto unit = bytes.subspan(at);
      const auto& record
        = records.emplace_back(RecordAt<SoftBodyBindingRecord>(unit, 0U));
      auto end = sizeof(SoftBodyBindingRecord);
      auto pinned = ReadVertexList(unit, end,
        {
          .count = record.pinned_vertex_count,
          .byte_offset = record.pinned_vertex_byte_offset,
        });
      auto kinematic = ReadVertexList(unit, end,
        {
          .count = record.kinematic_vertex_count,
          .byte_offset = record.kinematic_vertex_byte_offset,
        });
      vertices.push_back(
        { .pinned = std::move(pinned), .kinematic = std::move(kinematic) });
      at += end;
    }
  }

} // namespace

PhysicsSceneAsset::PhysicsSceneAsset(const AssetKey key,
  const std::span<const std::byte> data, const SourceOrigin source_origin)
  : Asset(key, source_origin)
{
  ParseAndValidate(data);
}

auto PhysicsSceneAsset::CheckTarget(const SceneAsset& scene) const -> void
{
  if (GetTargetSceneKey() != scene.GetAssetKey()) {
    throw CookedOpenError(
      CookedDefect::kInvalidReference, "physics sidecar targets another scene");
  }
  if (GetTargetNodeCount() != scene.GetNodes().size()) {
    throw CookedOpenError(CookedDefect::kInconsistent,
      "physics sidecar node count differs from its scene's");
  }
  if (!std::ranges::equal(
        GetTargetSceneContentHash(), scene.GetContentHash())) {
    throw CookedOpenError(CookedDefect::kDigestMismatch,
      "physics sidecar was cooked against another version of its scene");
  }
}

auto PhysicsSceneAsset::GetSoftBodyVertices(const std::size_t binding) const
  -> const SoftBodyVertexLists&
{
  return soft_body_vertices_.at(binding);
}

auto PhysicsSceneAsset::ParseAndValidate(const std::span<const std::byte> data)
  -> void
{
  ReadHeader(data);
  ReadStrings(data);
  ReadBindingTables(data);
  ValidateReserves();
  ValidateNodeTables();
  ValidateJoints();
  ValidateWheels();
}

auto PhysicsSceneAsset::ReadHeader(const std::span<const std::byte> data)
  -> void
{
  using physics::PhysicsSceneAssetDesc;
  if (data.size() < sizeof(PhysicsSceneAssetDesc)) {
    Refuse(CookedDefect::kTooSmall, "descriptor is smaller than its header");
  }
  desc_ = RecordAt<PhysicsSceneAssetDesc>(data, 0U);
  if (static_cast<AssetType>(desc_.header.asset_type)
    != AssetType::kPhysicsScene) {
    Refuse(CookedDefect::kWrongMagic, "descriptor is not a physics sidecar's");
  }
  if (desc_.header.version != physics::kPhysicsSceneAssetVersion) {
    Refuse(CookedDefect::kUnsupportedVersion,
      "descriptor version is not current; re-cook the physics sidecar");
  }
  // The backend selects the arm of every backend-scalar union of the
  // bindings, so a sidecar cooked for none cannot be read.
  if (!physics::IsCookedBackend(desc_.backend)) {
    Refuse(CookedDefect::kInvalidValue, "names no physics backend");
  }
  if (desc_.target_scene_key.IsNil()) {
    Refuse(CookedDefect::kMissingPart, "names no target scene");
  }
}

auto PhysicsSceneAsset::ReadStrings(const std::span<const std::byte> data)
  -> void
{
  auto strings = DescriptorStrings {};
  try {
    strings = DescriptorStrings::FromDescriptor(data, desc_.header.strings);
  } catch (const std::invalid_argument& error) {
    Refuse(CookedDefect::kInvalidString,
      std::string("string table is invalid: ") + error.what());
  }
  const auto name = strings.TryGet(desc_.header.name_offset);
  if (!name) {
    Refuse(CookedDefect::kInvalidString, "name offset is not a string");
  }
  SetAssetName(std::string { *name });
}

auto PhysicsSceneAsset::ReadBindingTables(const std::span<const std::byte> data)
  -> void
{
  using physics::PhysicsComponentTableDesc;
  const auto count = desc_.component_table_count;
  if (count == 0U) {
    return;
  }
  const auto directory_bytes
    = std::size_t { count } * sizeof(PhysicsComponentTableDesc);
  const auto directory_offset
    = std::size_t { desc_.component_table_directory_offset };
  if (!RangeFits(directory_offset, directory_bytes, data.size())) {
    Refuse(CookedDefect::kOutOfBounds,
      "binding directory lies outside the descriptor");
  }

  // The directory names each type the format defines at most once; only then
  // are its tables read.
  std::vector<PhysicsComponentTableDesc> entries;
  entries.reserve(count);
  std::unordered_set<physics::PhysicsBindingType> types;
  for (auto index = uint32_t { 0U }; index < count; ++index) {
    const auto& entry
      = entries.emplace_back(RecordAt<PhysicsComponentTableDesc>(data,
        directory_offset
          + (std::size_t { index } * sizeof(PhysicsComponentTableDesc))));
    if (!DefinesBindingType(entry.binding_type)) {
      Refuse(CookedDefect::kInvalidValue,
        "binding directory names a type the format does not define");
    }
    if (!types.insert(entry.binding_type).second) {
      Refuse(
        CookedDefect::kDuplicate, "binding directory names a table type twice");
    }
  }
  for (const auto& entry : entries) {
    const auto read = [&]<typename T>(std::vector<T>& records) -> void {
      if (entry.binding_type != BindingTraits<T>::kType
        || entry.table.count == 0U) {
        return;
      }
      if (entry.table.entry_size != sizeof(T)) {
        Refuse(CookedDefect::kWrongRowSize,
          "binding table rows are not its type's records");
      }
      if constexpr (std::is_same_v<T, physics::SoftBodyBindingRecord>) {
        ReadSoftBodies(data, entry.table, records, soft_body_vertices_);
      } else {
        records = ReadFixedTable<T>(data, entry.table);
      }
    };
    std::apply([&read](auto&... tables) -> void { (read(tables), ...); },
      binding_tables_);
  }
}

auto PhysicsSceneAsset::ValidateReserves() const -> void
{
  const auto backend = desc_.backend;
  const auto reserves_zero = [backend](const auto& bindings) -> bool {
    return std::ranges::all_of(
      bindings, [backend](const auto& binding) -> bool {
        return physics::HasZeroReserves(binding.backend_scalars, backend);
      });
  };
  const auto bindings_reserves_zero
    = reserves_zero(GetBindings<physics::RigidBodyBindingRecord>())
    && reserves_zero(GetBindings<physics::CharacterBindingRecord>())
    && reserves_zero(GetBindings<physics::JointBindingRecord>())
    && reserves_zero(GetBindings<physics::VehicleWheelBindingRecord>());
  if (!bindings_reserves_zero) {
    Refuse(CookedDefect::kReservedNotZero,
      "binding backend scalars hold non-zero reserved bytes");
  }
}

auto PhysicsSceneAsset::ValidateNodeTables() const -> void
{
  // A node table holds at most one binding per node, in node order, which
  // the lookup by node relies on.
  const auto validate
    = [this]<typename T>(const std::vector<T>& records) -> void {
    if constexpr (NodeBindingRecord<T>) {
      std::optional<pak::world::SceneNodeIndexT> previous;
      for (const auto& record : records) {
        if (record.node_index >= desc_.target_node_count) {
          Refuse(CookedDefect::kInvalidReference,
            "binding names a node its scene does not hold");
        }
        const auto repeats_its_node
          = previous.has_value() && record.node_index == *previous;
        const auto precedes_its_node
          = previous.has_value() && record.node_index < *previous;
        if (repeats_its_node) {
          Refuse(CookedDefect::kDuplicate, "node has two bindings of a type");
        }
        if (precedes_its_node) {
          Refuse(
            CookedDefect::kNotCanonical, "binding table is not in node order");
        }
        previous = record.node_index;
      }
    }
  };
  std::apply(
    [&validate](const auto&... tables) -> void { (validate(tables), ...); },
    binding_tables_);
}

auto PhysicsSceneAsset::ValidateJoints() const -> void
{
  // A joint binds a node to another node or to the world; the table is in
  // the order of the nodes it binds.
  const auto holds = [this](const pak::world::SceneNodeIndexT node) -> bool {
    return node < desc_.target_node_count;
  };
  std::optional<
    std::pair<pak::world::SceneNodeIndexT, pak::world::SceneNodeIndexT>>
    previous;
  for (const auto& joint : GetBindings<physics::JointBindingRecord>()) {
    const auto binds_held_nodes = holds(joint.node_index_a)
      && (joint.node_index_b == physics::kWorldAttachmentNodeIndex
        || holds(joint.node_index_b));
    if (!binds_held_nodes) {
      Refuse(CookedDefect::kInvalidReference,
        "joint names a node its scene does not hold");
    }
    const auto nodes = std::pair { joint.node_index_a, joint.node_index_b };
    if (previous.has_value() && nodes < *previous) {
      Refuse(CookedDefect::kNotCanonical, "joint table is not in node order");
    }
    previous = nodes;
  }
}

auto PhysicsSceneAsset::ValidateWheels() const -> void
{
  // The vehicles' slices partition the wheel table in vehicle order; a slice
  // holds its vehicle's wheels, one per role, in role order.
  const auto wheels = GetBindings<physics::VehicleWheelBindingRecord>();
  auto next = std::size_t { 0U };
  for (const auto& vehicle : GetBindings<physics::VehicleBindingRecord>()) {
    const auto offset = std::size_t { vehicle.wheel_slice_offset };
    const auto count = std::size_t { vehicle.wheel_slice_count };
    if (offset != next) {
      Refuse(CookedDefect::kNotCanonical,
        "vehicle wheel slice does not follow the previous one");
    }
    if (!RangeFits(offset, count, wheels.size())) {
      Refuse(CookedDefect::kOutOfBounds,
        "vehicle wheel slice lies outside the wheel table");
    }
    std::optional<std::pair<uint16_t, uint8_t>> previous;
    for (const auto& wheel : wheels.subspan(offset, count)) {
      const auto names_its_vehicle
        = wheel.vehicle_node_index == vehicle.node_index;
      if (!names_its_vehicle) {
        Refuse(CookedDefect::kInvalidReference,
          "wheel names another vehicle than its slice's");
      }
      if (wheel.wheel_node_index >= desc_.target_node_count) {
        Refuse(CookedDefect::kInvalidReference,
          "wheel names a node its scene does not hold");
      }
      const auto role
        = std::pair { wheel.axle_index, std::to_underlying(wheel.side) };
      if (previous.has_value() && role == *previous) {
        Refuse(CookedDefect::kDuplicate, "vehicle has two wheels of a role");
      }
      if (previous.has_value() && role < *previous) {
        Refuse(
          CookedDefect::kNotCanonical, "vehicle wheels are not in role order");
      }
      previous = role;
    }
    next = offset + count;
  }
  if (next != wheels.size()) {
    Refuse(
      CookedDefect::kInconsistent, "wheel table holds wheels of no vehicle");
  }
}

} // namespace oxygen::data
