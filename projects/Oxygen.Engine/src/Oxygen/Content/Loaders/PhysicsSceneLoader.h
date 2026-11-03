//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::loaders {

//! Loader function for physics scene sidecar assets.
/*!
 Reads the descriptor the context's reader holds, from its position to its
 end, and admits it as a `data::PhysicsSceneAsset`, which refuses a malformed
 sidecar with a `data::CookedFormatError`. The loader then checks what the
 sidecar references.

 @see LoadSceneAsset, data::PhysicsSceneAsset
*/
inline auto LoadPhysicsSceneAsset(const LoaderContext& context)
  -> std::unique_ptr<data::PhysicsSceneAsset>
{
  LOG_SCOPE_FUNCTION(1);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;
  auto bytes_res = reader.ReadBlob(reader.Remaining().get());
  CheckLoaderResult(bytes_res, "physics scene asset", "ReadBlob(descriptor)");

  auto asset = std::make_unique<data::PhysicsSceneAsset>(
    context.current_asset_key, *bytes_res,
    data::SourceOrigin {
      .key = context.source_key,
      .instance = context.source_instance,
    });
  if (context.asset_references) {
    namespace physics = data::pak::physics;
    std::vector<data::KeyReference> keys;
    const auto asset_key
      = [&keys](const data::AssetKey& key, const data::AssetType type) -> void {
      keys.push_back({
        .key = key,
        .kind = data::KeyReferenceKind::kAsset,
        .expected_type = type,
      });
    };
    const auto payload_key = [&keys](const data::AssetKey& key) -> void {
      keys.push_back({
        .key = key,
        .kind = data::KeyReferenceKind::kPhysicsResource,
        .expected_type = data::AssetType::kUnknown,
      });
    };
    keys.push_back({
      .key = asset->GetTargetSceneKey(),
      .kind = data::KeyReferenceKind::kLogical,
      .expected_type = data::AssetType::kScene,
    });
    for (const auto& body :
      asset->GetBindings<physics::RigidBodyBindingRecord>()) {
      asset_key(body.shape_asset_key, data::AssetType::kCollisionShape);
      asset_key(body.material_asset_key, data::AssetType::kPhysicsMaterial);
    }
    for (const auto& collider :
      asset->GetBindings<physics::ColliderBindingRecord>()) {
      asset_key(collider.shape_asset_key, data::AssetType::kCollisionShape);
      asset_key(collider.material_asset_key, data::AssetType::kPhysicsMaterial);
    }
    for (const auto& character :
      asset->GetBindings<physics::CharacterBindingRecord>()) {
      asset_key(character.shape_asset_key, data::AssetType::kCollisionShape);
      asset_key(
        character.inner_shape_asset_key, data::AssetType::kCollisionShape);
    }
    for (const auto& soft_body :
      asset->GetBindings<physics::SoftBodyBindingRecord>()) {
      payload_key(soft_body.topology_asset_key);
    }
    for (const auto& joint :
      asset->GetBindings<physics::JointBindingRecord>()) {
      payload_key(joint.constraint_asset_key);
    }
    for (const auto& vehicle :
      asset->GetBindings<physics::VehicleBindingRecord>()) {
      payload_key(vehicle.constraint_asset_key);
    }
    context.ValidateReferences({}, keys);
  }
  return asset;
}

} // namespace oxygen::content::loaders
