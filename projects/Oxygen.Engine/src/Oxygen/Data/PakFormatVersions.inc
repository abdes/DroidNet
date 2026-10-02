//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Authoritative current cooked-format versions. Native producers, readers and
// tools consume this catalog; readers accept these versions only.
OXDAT_FORMAT_VERSION(kPakVersion, uint16_t, 8)
OXDAT_FORMAT_VERSION(kPakCatalogVersion, uint32_t, 2)
OXDAT_FORMAT_VERSION(kLooseCookedIndexVersion, uint16_t, 3)
OXDAT_ASSET_VERSION(kMaterialAssetVersion, kMaterial, material, 4)
OXDAT_ASSET_VERSION(kGeometryAssetVersion, kGeometry, geometry, 3)
OXDAT_ASSET_VERSION(kSceneAssetVersion, kScene, scene, 10)
OXDAT_ASSET_VERSION(kScriptAssetVersion, kScript, script, 2)
OXDAT_ASSET_VERSION(kInputActionAssetVersion, kInputAction, input_action, 1)
OXDAT_ASSET_VERSION(kInputMappingContextAssetVersion, kInputMappingContext,
  input_mapping_context, 1)
OXDAT_ASSET_VERSION(
  kPhysicsMaterialAssetVersion, kPhysicsMaterial, physics_material, 1)
OXDAT_ASSET_VERSION(
  kCollisionShapeAssetVersion, kCollisionShape, collision_shape, 1)
OXDAT_ASSET_VERSION(kPhysicsSceneAssetVersion, kPhysicsScene, physics_scene, 1)
OXDAT_FORMAT_VERSION(kBrowseIndexVersion, uint32_t, 1)
OXDAT_FORMAT_VERSION(kExposureExtensionVersion, uint32_t, 1)
