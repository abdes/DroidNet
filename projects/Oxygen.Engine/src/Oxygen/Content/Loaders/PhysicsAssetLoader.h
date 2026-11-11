//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Content/CollisionShapeData.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::loaders {

inline auto LoadPhysicsMaterialDescriptor(const LoaderContext& context)
  -> data::pak::physics::PhysicsMaterialAssetDesc
{
  const auto base_pos = context.desc_reader->Position();
  CheckLoaderResult(base_pos, "physics material", "position");
  const auto bytes = context.desc_reader->ReadBlob(
    sizeof(data::pak::physics::PhysicsMaterialAssetDesc));
  CheckLoaderResult(bytes, "physics material", "descriptor");
  const auto parsed
    = serio::CopyAt<data::pak::physics::PhysicsMaterialAssetDesc>(*bytes);
  CheckLoaderResult(parsed, "physics material", "descriptor");
  const auto descriptor = *parsed;
  if (descriptor.header.asset_type
    != static_cast<uint8_t>(data::AssetType::kPhysicsMaterial)) {
    throw data::CookedFormatError(data::CookedDefect::kWrongMagic,
      "Physics material descriptor type/version is invalid");
  }
  if (descriptor.header.version
    != data::pak::physics::kPhysicsMaterialAssetVersion) {
    throw data::CookedFormatError(data::CookedDefect::kUnsupportedVersion,
      "Physics material descriptor type/version is invalid");
  }
  static_cast<void>(ResolveDescriptorString(
    ReadDescriptorStrings(*context.desc_reader, *base_pos, descriptor.header),
    descriptor.header.name_offset, "asset name"));
  SkipDescriptorStrings(*context.desc_reader, *base_pos, descriptor.header);
  context.ValidateReferences({}, {});
  return descriptor;
}

//! The payload a shape or compound child of `type` carries; `kInvalid` for an
//! analytic shape, nothing for a type no shape or child has.
inline auto RequiredShapePayload(const data::pak::physics::ShapeType type)
  -> std::optional<data::pak::physics::ShapePayloadType>
{
  namespace physics = data::pak::physics;
  switch (type) {
  case physics::ShapeType::kConvexHull:
    return physics::ShapePayloadType::kConvex;
  case physics::ShapeType::kTriangleMesh:
    return physics::ShapePayloadType::kMesh;
  case physics::ShapeType::kHeightField:
    return physics::ShapePayloadType::kHeightField;
  case physics::ShapeType::kSphere:
  case physics::ShapeType::kCapsule:
  case physics::ShapeType::kBox:
  case physics::ShapeType::kCylinder:
  case physics::ShapeType::kCone:
  case physics::ShapeType::kPlane:
  case physics::ShapeType::kWorldBoundary:
  case physics::ShapeType::kCompound:
    return physics::ShapePayloadType::kInvalid;
  case physics::ShapeType::kInvalid:
  default:
    return std::nullopt;
  }
}

inline auto LoadCollisionShapeDescriptor(const LoaderContext& context)
  -> CollisionShapeData
{
  namespace physics = data::pak::physics;
  auto& reader = *context.desc_reader;
  const auto packed = reader.ScopedAlignment(1U);
  const auto base_pos = reader.Position();
  CheckLoaderResult(base_pos, "collision shape", "position");
  const auto bytes = reader.ReadBlob(sizeof(physics::CollisionShapeAssetDesc));
  CheckLoaderResult(bytes, "collision shape", "descriptor");
  const auto parsed = serio::CopyAt<physics::CollisionShapeAssetDesc>(*bytes);
  CheckLoaderResult(parsed, "collision shape", "descriptor");
  const auto descriptor = *parsed;
  if (descriptor.header.asset_type
    != static_cast<uint8_t>(data::AssetType::kCollisionShape)) {
    throw data::CookedFormatError(data::CookedDefect::kWrongMagic,
      "Collision shape descriptor type/version is invalid");
  }
  if (descriptor.header.version != physics::kCollisionShapeAssetVersion) {
    throw data::CookedFormatError(data::CookedDefect::kUnsupportedVersion,
      "Collision shape descriptor type/version is invalid");
  }
  static_cast<void>(ResolveDescriptorString(
    ReadDescriptorStrings(reader, *base_pos, descriptor.header),
    descriptor.header.name_offset, "asset name"));
  if (descriptor.material_asset_key.IsNil()) {
    throw ContentLoadError(ContentLoadFailure::kContentInvalid,
      "Collision shape requires a physics material key");
  }
  const auto required_payload = RequiredShapePayload(descriptor.shape_type);
  if (!required_payload.has_value()) {
    throw data::CookedFormatError(
      data::CookedDefect::kInvalidValue, "Collision shape kind is invalid");
  }
  const auto reserves_zero = physics::HasZeroReserves(descriptor);
  if (!reserves_zero) {
    throw data::CookedFormatError(data::CookedDefect::kReservedNotZero,
      "Collision shape reserved bytes are not zero");
  }
  const auto carries_payload
    = *required_payload != physics::ShapePayloadType::kInvalid;
  const auto payload_matches_kind
    = descriptor.cooked_shape_ref.payload_type == *required_payload
    && carries_payload != descriptor.cooked_shape_ref.payload_asset_key.IsNil();
  if (!payload_matches_kind) {
    throw data::CookedFormatError(data::CookedDefect::kInconsistent,
      "Collision shape payload does not match its kind");
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
  auto out = CollisionShapeData { .descriptor = descriptor };
  if (descriptor.shape_type == physics::ShapeType::kCompound) {
    const auto& compound = descriptor.shape_params.compound;
    if (compound.child_count == 0U) {
      throw data::CookedFormatError(
        data::CookedDefect::kInconsistent, "Compound shape has no children");
    }
    if (compound.child_byte_offset < sizeof(descriptor)) {
      throw data::CookedFormatError(data::CookedDefect::kOverlap,
        "Compound children overlap the shape descriptor");
    }
    CheckLoaderResult(
      reader.Forward(compound.child_byte_offset - sizeof(descriptor)),
      "collision shape", "compound child offset");
    // Each child is read before the next is allocated: a count no
    // descriptor holds fails on the first missing record.
    for (uint32_t index = 0U; index < compound.child_count; ++index) {
      const auto child_bytes
        = reader.ReadBlob(sizeof(physics::CompoundShapeChildDesc));
      CheckLoaderResult(child_bytes, "collision shape", "compound child");
      const auto child
        = serio::CopyAt<physics::CompoundShapeChildDesc>(*child_bytes);
      CheckLoaderResult(child, "collision shape", "compound child");
      if (!physics::HasZeroReserves(*child)) {
        throw data::CookedFormatError(data::CookedDefect::kReservedNotZero,
          "Compound child reserved bytes are not zero");
      }
      const auto child_type
        = static_cast<physics::ShapeType>(child->shape_type);
      const auto child_payload = RequiredShapePayload(child_type);
      if (!child_payload.has_value()
        || child_type == physics::ShapeType::kCompound) {
        throw data::CookedFormatError(
          data::CookedDefect::kInvalidValue, "Compound child kind is invalid");
      }
      const auto child_carries_payload
        = *child_payload != physics::ShapePayloadType::kInvalid;
      const auto child_payload_matches_kind
        = child_carries_payload != child->payload_asset_key.IsNil();
      if (!child_payload_matches_kind) {
        throw data::CookedFormatError(data::CookedDefect::kInconsistent,
          "Compound child payload does not match its kind");
      }
      if (child_carries_payload) {
        keys.push_back({
          .key = child->payload_asset_key,
          .kind = data::KeyReferenceKind::kPhysicsResource,
          .expected_type = data::AssetType::kUnknown,
        });
      }
      out.children.push_back(*child);
    }
  }
  SkipDescriptorStrings(reader, *base_pos, descriptor.header);
  context.ValidateReferences({}, keys);
  return out;
}

} // namespace oxygen::content::loaders
