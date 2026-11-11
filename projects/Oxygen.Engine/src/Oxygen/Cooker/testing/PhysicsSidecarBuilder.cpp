//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::cooker::testing {

namespace {

  namespace physics = data::pak::physics;

  //! One binding table: its type and its packed records.
  struct Table final {
    physics::PhysicsBindingType type = physics::PhysicsBindingType::kUnknown;
    uint32_t entry_size = 0;
    uint32_t count = 0;
    std::vector<std::byte> bytes;
  };

  //! Adds the table of `records`, sorted by node as the cooker sorts them.
  template <typename Record, typename Node>
  auto AddTable(std::vector<Table>& tables,
    const physics::PhysicsBindingType type, std::vector<Record> records,
    Node node) -> void
  {
    if (records.empty()) {
      return;
    }
    std::ranges::stable_sort(records, {}, node);
    const auto bytes = std::as_bytes(std::span(records));
    tables.push_back({
      .type = type,
      .entry_size = sizeof(Record),
      .count = static_cast<uint32_t>(records.size()),
      .bytes = { bytes.begin(), bytes.end() },
    });
  }

  //! Appends the bytes of `values` to `bytes`.
  template <typename T>
  auto Append(std::vector<std::byte>& bytes, const std::span<T> values) -> void
  {
    const auto part = std::as_bytes(values);
    bytes.insert(bytes.end(), part.begin(), part.end());
  }

  //! The wheels in vehicle, axle and then side order, as the cooker writes
  //! them; `vehicles`, in node order, record the slice of each.
  auto SliceWheels(std::vector<physics::VehicleBindingRecord>& vehicles,
    std::vector<physics::VehicleWheelBindingRecord> wheels)
    -> std::vector<physics::VehicleWheelBindingRecord>
  {
    std::ranges::stable_sort(
      vehicles, {}, &physics::VehicleBindingRecord::node_index);
    std::ranges::stable_sort(
      wheels, {}, [](const physics::VehicleWheelBindingRecord& wheel) -> auto {
        return std::tuple { wheel.vehicle_node_index, wheel.axle_index,
          std::to_underlying(wheel.side) };
      });
    auto sliced = std::size_t { 0U };
    for (auto& vehicle : vehicles) {
      const auto [first, last]
        = std::ranges::equal_range(wheels, vehicle.node_index, {},
          &physics::VehicleWheelBindingRecord::vehicle_node_index);
      vehicle.wheel_slice_offset
        = static_cast<uint32_t>(std::distance(wheels.begin(), first));
      vehicle.wheel_slice_count
        = static_cast<uint32_t>(std::distance(first, last));
      sliced += vehicle.wheel_slice_count;
    }
    if (sliced != wheels.size()) {
      throw std::invalid_argument("a wheel names a node without a vehicle");
    }
    return wheels;
  }

  //! The table of the soft bodies `bodies` in node order, each record
  //! followed by its vertex lists, as the cooker writes them.
  template <typename SoftBody>
  auto SoftBodyTable(std::vector<SoftBody> bodies) -> Table
  {
    std::ranges::stable_sort(bodies, {},
      [](const SoftBody& body) -> auto { return body.record.node_index; });
    auto table = Table {
      .type = physics::PhysicsBindingType::kSoftBody,
      .entry_size = sizeof(physics::SoftBodyBindingRecord),
      .count = static_cast<uint32_t>(bodies.size()),
      .bytes = {},
    };
    for (const auto& [record, vertices] : bodies) {
      // Each list follows the record, pinned first, at an offset from the
      // record's start; an empty list has no offset.
      auto unit = record;
      auto end = static_cast<uint32_t>(sizeof(unit));
      const auto place = [&end](const std::vector<uint32_t>& list) -> uint32_t {
        if (list.empty()) {
          return 0U;
        }
        return std::exchange(
          end, end + static_cast<uint32_t>(list.size() * sizeof(uint32_t)));
      };
      unit.pinned_vertex_count = static_cast<uint32_t>(vertices.pinned.size());
      unit.pinned_vertex_byte_offset = place(vertices.pinned);
      unit.kinematic_vertex_count
        = static_cast<uint32_t>(vertices.kinematic.size());
      unit.kinematic_vertex_byte_offset = place(vertices.kinematic);
      Append(table.bytes, std::span(&unit, 1U));
      Append(table.bytes, std::span(vertices.pinned));
      Append(table.bytes, std::span(vertices.kinematic));
    }
    return table;
  }

} // namespace

auto PhysicsSidecarBuilder::Claim(const SceneNode node)
  -> data::pak::world::SceneNodeIndexT
{
  if (owner_.has_value() && *owner_ != node.Owner()) {
    throw std::invalid_argument(
      "the bindings of a sidecar name the nodes of one scene");
  }
  owner_ = node.Owner();
  highest_node_ = std::max(highest_node_, node.Index());
  return node.Index();
}

auto PhysicsSidecarBuilder::RigidBody(const SceneNode node,
  const data::AssetKey& shape, const data::AssetKey& material,
  physics::RigidBodyBindingRecord record) -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  record.shape_asset_key = shape;
  record.material_asset_key = material;
  rigid_bodies_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::Collider(const SceneNode node,
  const data::AssetKey& shape, const data::AssetKey& material,
  physics::ColliderBindingRecord record) -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  record.shape_asset_key = shape;
  record.material_asset_key = material;
  colliders_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::Character(const SceneNode node,
  const data::AssetKey& shape, physics::CharacterBindingRecord record)
  -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  record.shape_asset_key = shape;
  characters_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::SoftBody(const SceneNode node,
  const data::AssetKey& topology, physics::SoftBodyBindingRecord record,
  data::SoftBodyVertexLists vertices) -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  record.topology_asset_key = topology;
  soft_bodies_.push_back({ .record = record, .vertices = std::move(vertices) });
  return *this;
}

auto PhysicsSidecarBuilder::Joint(const SceneNode a,
  const std::optional<SceneNode> b, const data::AssetKey& constraint,
  physics::JointBindingRecord record) -> PhysicsSidecarBuilder&
{
  record.node_index_a = Claim(a);
  record.node_index_b
    = b.has_value() ? Claim(*b) : physics::kWorldAttachmentNodeIndex;
  record.constraint_asset_key = constraint;
  joints_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::Vehicle(const SceneNode node,
  const data::AssetKey& constraint, physics::VehicleBindingRecord record)
  -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  record.constraint_asset_key = constraint;
  record.wheel_slice_offset = 0U;
  record.wheel_slice_count = 0U;
  vehicles_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::Wheel(
  const SceneNode vehicle, const VehicleWheel& wheel) -> PhysicsSidecarBuilder&
{
  auto record = wheel.record;
  record.vehicle_node_index = Claim(vehicle);
  record.wheel_node_index = Claim(wheel.node);
  record.axle_index = wheel.axle;
  record.side = wheel.side;
  wheels_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::Aggregate(const SceneNode node,
  physics::AggregateBindingRecord record) -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  aggregates_.push_back(record);
  return *this;
}

auto PhysicsSidecarBuilder::Backend(
  const data::pak::physics::PhysicsBackend backend) -> PhysicsSidecarBuilder&
{
  backend_ = backend;
  return *this;
}

auto PhysicsSidecarBuilder::Bytes(const data::AssetKey& scene_key,
  const std::span<const std::byte> scene_bytes,
  const std::string_view name) const -> std::vector<std::byte>
{
  const auto scene = data::SceneAsset(scene_key, scene_bytes);
  const auto node_count = static_cast<uint32_t>(scene.GetNodes().size());
  if (owner_.has_value() && highest_node_ >= node_count) {
    throw std::invalid_argument("a binding names a node the scene '"
      + to_string(scene_key) + "' does not hold");
  }

  using physics::PhysicsBindingType;
  auto tables = std::vector<Table> {};
  AddTable(tables, PhysicsBindingType::kRigidBody, rigid_bodies_,
    &physics::RigidBodyBindingRecord::node_index);
  AddTable(tables, PhysicsBindingType::kCollider, colliders_,
    &physics::ColliderBindingRecord::node_index);
  AddTable(tables, PhysicsBindingType::kCharacter, characters_,
    &physics::CharacterBindingRecord::node_index);
  if (!soft_bodies_.empty()) {
    tables.push_back(SoftBodyTable(soft_bodies_));
  }
  AddTable(tables, PhysicsBindingType::kJoint, joints_,
    [](const physics::JointBindingRecord& joint) -> auto {
      return std::pair { joint.node_index_a, joint.node_index_b };
    });
  auto vehicles = vehicles_;
  const auto wheels = SliceWheels(vehicles, wheels_);
  AddTable(tables, PhysicsBindingType::kVehicle, vehicles,
    &physics::VehicleBindingRecord::node_index);
  AddTable(tables, PhysicsBindingType::kVehicleWheel, wheels,
    &physics::VehicleWheelBindingRecord::vehicle_node_index);
  AddTable(tables, PhysicsBindingType::kAggregate, aggregates_,
    &physics::AggregateBindingRecord::node_index);
  std::ranges::stable_sort(tables, {}, [](const Table& table) -> uint32_t {
    return static_cast<uint32_t>(table.type);
  });

  auto desc = physics::PhysicsSceneAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
  desc.header.version = physics::kPhysicsSceneAssetVersion;
  auto strings = data::DescriptorStringsBuilder {};
  desc.header.name_offset = strings.Add(name);
  desc.target_scene_key = scene_key;
  desc.target_node_count = node_count;
  desc.backend = backend_;
  std::ranges::copy(data::ComputeNodeLayoutHash(scene.GetNodes()),
    std::begin(desc.target_scene_layout_hash));
  desc.component_table_count = static_cast<uint32_t>(tables.size());
  desc.component_table_directory_offset
    = tables.empty() ? 0U : static_cast<data::pak::core::OffsetT>(sizeof(desc));

  auto directory = std::vector<physics::PhysicsComponentTableDesc> {};
  auto offset = uint64_t { sizeof(desc) }
    + (tables.size() * sizeof(physics::PhysicsComponentTableDesc));
  for (const auto& table : tables) {
    directory.push_back({
      .binding_type = table.type,
      .table = {
        .offset = static_cast<data::pak::core::OffsetT>(offset),
        .count = table.count,
        .entry_size = table.entry_size,
      },
    });
    offset += table.bytes.size();
  }

  auto bytes = std::vector<std::byte> {};
  const auto append = [&bytes](const std::span<const std::byte> part) -> void {
    bytes.insert(bytes.end(), part.begin(), part.end());
  };
  append(std::as_bytes(std::span(&desc, 1U)));
  append(std::as_bytes(std::span(directory)));
  for (const auto& table : tables) {
    append(table.bytes);
  }
  data::AppendDescriptorStrings(bytes, strings);
  data::StampDescriptorHash(bytes);

  try {
    static_cast<void>(data::PhysicsSceneAsset(
      data::AssetKey::FromVirtualPath("/" + std::string(name) + ".opscene"),
      std::span<const std::byte>(bytes)));
  } catch (const std::exception& refused) {
    throw std::logic_error("the reader refuses the sidecar '"
      + std::string(name) + "': " + refused.what());
  }
  return bytes;
}

auto PhysicsSidecarBuilder::References(const data::AssetKey& scene_key) const
  -> data::AssetReferences
{
  auto references = content::import::AssetReferenceBuilder {};
  references.AddLogical(scene_key, data::AssetType::kScene);
  for (const auto& record : rigid_bodies_) {
    references.AddAsset(
      record.shape_asset_key, data::AssetType::kCollisionShape);
    references.AddAsset(
      record.material_asset_key, data::AssetType::kPhysicsMaterial);
  }
  for (const auto& record : colliders_) {
    references.AddAsset(
      record.shape_asset_key, data::AssetType::kCollisionShape);
    references.AddAsset(
      record.material_asset_key, data::AssetType::kPhysicsMaterial);
  }
  for (const auto& record : characters_) {
    references.AddAsset(
      record.shape_asset_key, data::AssetType::kCollisionShape);
    references.AddAsset(
      record.inner_shape_asset_key, data::AssetType::kCollisionShape);
  }
  for (const auto& body : soft_bodies_) {
    references.AddPhysicsResource(body.record.topology_asset_key);
  }
  for (const auto& record : joints_) {
    references.AddPhysicsResource(record.constraint_asset_key);
  }
  for (const auto& record : vehicles_) {
    references.AddPhysicsResource(record.constraint_asset_key);
  }
  return std::move(references).Build();
}

} // namespace oxygen::cooker::testing
