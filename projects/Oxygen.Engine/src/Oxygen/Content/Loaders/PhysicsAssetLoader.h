//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::loaders {

inline auto LoadPhysicsMaterialDescriptor(const LoaderContext& context)
  -> data::pak::physics::PhysicsMaterialAssetDesc
{
  data::pak::physics::PhysicsMaterialAssetDesc descriptor {};
  const auto bytes = context.desc_reader->ReadBlob(sizeof(descriptor));
  CheckLoaderResult(bytes, "physics material", "descriptor");
  std::memcpy(&descriptor, bytes->data(), sizeof(descriptor));
  if (descriptor.header.asset_type
      != static_cast<uint8_t>(data::AssetType::kPhysicsMaterial)
    || descriptor.header.version
      != data::pak::physics::kPhysicsMaterialAssetVersion) {
    throw std::runtime_error(
      "Physics material descriptor type/version is invalid");
  }
  context.ValidateReferences({}, {});
  return descriptor;
}

inline auto LoadCollisionShapeDescriptor(const LoaderContext& context)
  -> data::pak::physics::CollisionShapeAssetDesc
{
  namespace physics = data::pak::physics;
  auto& reader = *context.desc_reader;
  const auto packed = reader.ScopedAlignment(1U);
  physics::CollisionShapeAssetDesc descriptor {};
  const auto bytes = reader.ReadBlob(sizeof(descriptor));
  CheckLoaderResult(bytes, "collision shape", "descriptor");
  std::memcpy(&descriptor, bytes->data(), sizeof(descriptor));
  if (descriptor.header.asset_type
      != static_cast<uint8_t>(data::AssetType::kCollisionShape)
    || descriptor.header.version != physics::kCollisionShapeAssetVersion) {
    throw std::runtime_error(
      "Collision shape descriptor type/version is invalid");
  }
  if (descriptor.material_asset_key.IsNil()) {
    throw std::runtime_error("Collision shape requires a physics material key");
  }
  auto required_payload = physics::ShapePayloadType::kInvalid;
  switch (descriptor.shape_type) {
  case physics::ShapeType::kConvexHull:
    required_payload = physics::ShapePayloadType::kConvex;
    break;
  case physics::ShapeType::kTriangleMesh:
    required_payload = physics::ShapePayloadType::kMesh;
    break;
  case physics::ShapeType::kHeightField:
    required_payload = physics::ShapePayloadType::kHeightField;
    break;
  case physics::ShapeType::kSphere:
  case physics::ShapeType::kCapsule:
  case physics::ShapeType::kBox:
  case physics::ShapeType::kCylinder:
  case physics::ShapeType::kCone:
  case physics::ShapeType::kPlane:
  case physics::ShapeType::kWorldBoundary:
  case physics::ShapeType::kCompound:
    break;
  case physics::ShapeType::kInvalid:
  default:
    throw std::runtime_error("Collision shape kind is invalid");
  }
  if (descriptor.cooked_shape_ref.payload_type != required_payload
    || (required_payload != physics::ShapePayloadType::kInvalid
      && descriptor.cooked_shape_ref.payload_asset_key.IsNil())
    || (required_payload == physics::ShapePayloadType::kInvalid
      && !descriptor.cooked_shape_ref.payload_asset_key.IsNil())) {
    throw std::runtime_error("Collision shape payload does not match its kind");
  }
  std::vector<data::KeyReference> keys {
    {
      .key = descriptor.material_asset_key,
      .kind = data::KeyReferenceKind::kAsset,
      .expected_type = data::AssetType::kPhysicsMaterial,
    },
    {
      .key = descriptor.cooked_shape_ref.payload_asset_key,
      .kind = data::KeyReferenceKind::kPhysicsResource,
      .expected_type = data::AssetType::kUnknown,
    },
  };
  if (descriptor.shape_type == physics::ShapeType::kCompound) {
    const auto& compound = descriptor.shape_params.compound;
    if (compound.child_count == 0U) {
      if (compound.child_byte_offset != 0U) {
        throw std::runtime_error("Empty compound child range is not canonical");
      }
    } else {
      if (compound.child_byte_offset < sizeof(descriptor)) {
        throw std::runtime_error(
          "Compound children overlap the shape descriptor");
      }
      CheckLoaderResult(
        reader.Forward(compound.child_byte_offset - sizeof(descriptor)),
        "collision shape", "compound child offset");
      for (uint32_t index = 0U; index < compound.child_count; ++index) {
        physics::CompoundShapeChildDesc child {};
        const auto child_bytes = reader.ReadBlob(sizeof(child));
        CheckLoaderResult(child_bytes, "collision shape", "compound child");
        std::memcpy(&child, child_bytes->data(), sizeof(child));
        keys.push_back({
          .key = child.payload_asset_key,
          .kind = data::KeyReferenceKind::kPhysicsResource,
          .expected_type = data::AssetType::kUnknown,
        });
      }
    }
  }
  context.ValidateReferences({}, keys);
  return descriptor;
}

} // namespace oxygen::content::loaders
