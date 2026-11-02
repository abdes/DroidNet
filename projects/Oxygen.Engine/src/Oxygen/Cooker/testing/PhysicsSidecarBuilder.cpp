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
  const data::AssetKey& topology, physics::SoftBodyBindingRecord record)
  -> PhysicsSidecarBuilder&
{
  record.node_index = Claim(node);
  record.topology_asset_key = topology;
  record.pinned_vertex_count = 0U;
  record.pinned_vertex_byte_offset = 0U;
  record.kinematic_vertex_count = 0U;
  record.kinematic_vertex_byte_offset = 0U;
  soft_bodies_.push_back(record);
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
  const auto node_count = static_cast<uint32_t>(
    data::SceneAsset(scene_key, scene_bytes).GetNodes().size());
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
  AddTable(tables, PhysicsBindingType::kSoftBody, soft_bodies_,
    &physics::SoftBodyBindingRecord::node_index);
  AddTable(tables, PhysicsBindingType::kJoint, joints_,
    &physics::JointBindingRecord::node_index_a);
  AddTable(tables, PhysicsBindingType::kVehicle, vehicles_,
    &physics::VehicleBindingRecord::node_index);
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
  const auto hash = data::ComputeDescriptorHash(scene_bytes);
  std::ranges::copy(hash, std::begin(desc.target_scene_content_hash));
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
  for (const auto& record : soft_bodies_) {
    references.AddPhysicsResource(record.topology_asset_key);
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
