//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Authoritative current cooked-format versions. Native producers, readers and
// tools consume this catalog; readers accept these versions only. A format's
// spec name is how tools report it.
OXDAT_FORMAT_VERSION(kPakVersion, pak, uint16_t, 12)
OXDAT_FORMAT_VERSION(kPakCatalogVersion, pak_catalog, uint32_t, 5)
OXDAT_FORMAT_VERSION(kLooseCookedIndexVersion, loose_cooked_index, uint16_t, 9)
OXDAT_FORMAT_VERSION(
  kTextureResourceDescriptorVersion, texture_descriptor, uint16_t, 3)
OXDAT_FORMAT_VERSION(
  kBufferResourceDescriptorVersion, buffer_descriptor, uint16_t, 5)
OXDAT_ASSET_VERSION(kMaterialAssetVersion, kMaterial, material, 6)
OXDAT_ASSET_VERSION(kGeometryAssetVersion, kGeometry, geometry, 8)
OXDAT_ASSET_VERSION(kSceneAssetVersion, kScene, scene, 18)
OXDAT_ASSET_VERSION(kScriptAssetVersion, kScript, script, 6)
OXDAT_ASSET_VERSION(kInputActionAssetVersion, kInputAction, input_action, 3)
OXDAT_ASSET_VERSION(kInputMappingContextAssetVersion, kInputMappingContext,
  input_mapping_context, 4)
OXDAT_ASSET_VERSION(
  kPhysicsMaterialAssetVersion, kPhysicsMaterial, physics_material, 3)
OXDAT_ASSET_VERSION(
  kCollisionShapeAssetVersion, kCollisionShape, collision_shape, 3)
OXDAT_ASSET_VERSION(kPhysicsSceneAssetVersion, kPhysicsScene, physics_scene, 4)
OXDAT_FORMAT_VERSION(kBrowseIndexVersion, browse_index, uint32_t, 2)
OXDAT_FORMAT_VERSION(kExposureExtensionVersion, exposure_extension, uint32_t, 2)

// Revisions of what each importer cooks. An importer's revision goes up when
// the content it cooks from the same inputs changes while every format above
// stays the same, so that content an earlier revision cooked is cooked again.
// The importer is the enumerator of `content::import::Importer`; its spec name
// is how the ImportTool reports it.
OXDAT_IMPORTER_REVISION(kGltf, gltf, 14)
OXDAT_IMPORTER_REVISION(kFbx, fbx, 14)
OXDAT_IMPORTER_REVISION(kTexture, texture, 6)
OXDAT_IMPORTER_REVISION(kBufferContainer, buffer_container, 4)
OXDAT_IMPORTER_REVISION(kMaterialDescriptor, material_descriptor, 4)
OXDAT_IMPORTER_REVISION(
  kPhysicsMaterialDescriptor, physics_material_descriptor, 3)
OXDAT_IMPORTER_REVISION(
  kCollisionShapeDescriptor, collision_shape_descriptor, 3)
OXDAT_IMPORTER_REVISION(kGeometryDescriptor, geometry_descriptor, 8)
OXDAT_IMPORTER_REVISION(kSceneDescriptor, scene_descriptor, 3)
OXDAT_IMPORTER_REVISION(kScriptAsset, script, 4)
OXDAT_IMPORTER_REVISION(kScriptingSidecar, script_sidecar, 3)
OXDAT_IMPORTER_REVISION(kPhysicsSidecar, physics_sidecar, 6)
OXDAT_IMPORTER_REVISION(kInput, input, 3)
