//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/PhysicsSceneAsset.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace oxygen::data {

PhysicsSceneAsset::PhysicsSceneAsset(
  AssetKey key, std::span<const std::byte> data)
  : Asset(key)
  , data_(data)
{
  ParseAndValidate();
}

PhysicsSceneAsset::PhysicsSceneAsset(AssetKey key, std::vector<std::byte> data)
  : Asset(key)
  , owned_data_(std::make_shared<std::vector<std::byte>>(std::move(data)))
  , data_(owned_data_->data(), owned_data_->size())
{
  ParseAndValidate();
}

auto PhysicsSceneAsset::ParseAndValidate() -> void
{
  if (data_.size() < sizeof(pak::physics::PhysicsSceneAssetDesc)) {
    throw std::runtime_error(
      "PhysicsSceneAsset data too small for descriptor header");
  }

  std::memcpy(
    &desc_, data_.data(), sizeof(pak::physics::PhysicsSceneAssetDesc));

  if (static_cast<data::AssetType>(desc_.header.asset_type)
    != data::AssetType::kPhysicsScene) {
    throw std::runtime_error(
      "PhysicsSceneAsset: invalid asset_type in descriptor header");
  }

  if (desc_.header.version != pak::physics::kPhysicsSceneAssetVersion) {
    throw std::runtime_error(
      "PhysicsSceneAsset: unsupported asset descriptor version");
  }

  auto range_ok
    = [](const size_t offset, const size_t size, const size_t total) {
        return offset <= total && size <= (total - offset);
      };

  binding_tables_.clear();

  if (desc_.component_table_count == 0) {
    return; // Valid empty sidecar (scene has no physics objects)
  }

  const size_t dir_bytes = static_cast<size_t>(desc_.component_table_count)
    * sizeof(pak::physics::PhysicsComponentTableDesc);

  if (!range_ok(static_cast<size_t>(desc_.component_table_directory_offset),
        dir_bytes, data_.size())) {
    throw std::runtime_error(
      "PhysicsSceneAsset: binding table directory out of bounds");
  }

  const auto dir_span = data_.subspan(
    static_cast<size_t>(desc_.component_table_directory_offset), dir_bytes);

  binding_tables_.reserve(desc_.component_table_count);

  for (uint32_t i = 0; i < desc_.component_table_count; ++i) {
    pak::physics::PhysicsComponentTableDesc entry {};
    std::memcpy(&entry,
      dir_span
        .subspan(static_cast<size_t>(i)
            * sizeof(pak::physics::PhysicsComponentTableDesc),
          sizeof(pak::physics::PhysicsComponentTableDesc))
        .data(),
      sizeof(entry));

    if (entry.table.count == 0) {
      continue;
    }

    const size_t table_bytes
      = static_cast<size_t>(entry.table.count) * entry.table.entry_size;

    if (!range_ok(
          static_cast<size_t>(entry.table.offset), table_bytes, data_.size())) {
      throw std::runtime_error(
        "PhysicsSceneAsset: binding table data out of bounds");
    }

    // Validate known record sizes per PhysicsBindingType.
    using BT = pak::physics::PhysicsBindingType;
    switch (entry.binding_type) {
    case BT::kRigidBody:
      if (entry.table.entry_size
        != sizeof(pak::physics::RigidBodyBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: RigidBodyBindingRecord entry_size mismatch");
      }
      break;
    case BT::kCollider:
      if (entry.table.entry_size
        != sizeof(pak::physics::ColliderBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: ColliderBindingRecord entry_size mismatch");
      }
      break;
    case BT::kCharacter:
      if (entry.table.entry_size
        != sizeof(pak::physics::CharacterBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: CharacterBindingRecord entry_size mismatch");
      }
      break;
    case BT::kSoftBody:
      if (entry.table.entry_size
        != sizeof(pak::physics::SoftBodyBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: SoftBodyBindingRecord entry_size mismatch");
      }
      break;
    case BT::kJoint:
      if (entry.table.entry_size != sizeof(pak::physics::JointBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: JointBindingRecord entry_size mismatch");
      }
      break;
    case BT::kVehicle:
      if (entry.table.entry_size
        != sizeof(pak::physics::VehicleBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: VehicleBindingRecord entry_size mismatch");
      }
      break;
    case BT::kAggregate:
      if (entry.table.entry_size
        != sizeof(pak::physics::AggregateBindingRecord)) {
        throw std::runtime_error(
          "PhysicsSceneAsset: AggregateBindingRecord entry_size mismatch");
      }
      break;
    case BT::kUnknown:
    default:
      LOG_F(WARNING,
        "PhysicsSceneAsset: unknown binding type 0x{:08X} — skipping",
        static_cast<uint32_t>(entry.binding_type));
      continue;
    }

    binding_tables_.push_back({ .type = entry.binding_type,
      .offset = entry.table.offset,
      .count = entry.table.count,
      .entry_size = entry.table.entry_size });
  }
}

} // namespace oxygen::data
