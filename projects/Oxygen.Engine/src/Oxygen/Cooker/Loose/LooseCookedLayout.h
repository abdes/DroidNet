//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

#include <Oxygen/Content/Layout.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import {

//! Configurable relative paths for a loose cooked container.
/*!
 The runtime Content loader expects a `container.index.bin` index at the root
 plus optional resource table/data files referenced by that index. The loader
 follows whatever paths the index records.

 ### What the layout places

 The cooked layout is the identity: a job that cooks one asset (a material,
 geometry, scene, script, texture, physics material or collision shape
 descriptor) writes its descriptor at the virtual path its request names, so
 the descriptor of `/Mount/X/Y.ext` lives at `X/Y.ext` under the cooked root.
 The layout has no folder for those.

 An input source is a container whose products are named under its identity
 (`X/Player.input/Jump.oiact`); the layout has no folder for them either.

 The layout still names the products of models by type: their materials,
 geometry, scenes, textures and buffers. It also places the shared resource
 tables.

 - Keep resource blob files (tables/data) under a dedicated resources folder.
 - Keep a virtual mount root that anchors virtual paths recorded in the index.
 - Ensure all configured *physical* paths are container-relative and use
   forward slashes.

 @warning The runtime mount path is rooted at `container.index.bin`.
  Today, mounting assumes this filename at the container root.

 @see oxygen::data::loose_cooked::IndexHeader
 @see oxygen::data::loose_cooked::FileRecord
*/
//! Layout description for loose cooked output.
struct LooseCookedLayout final : Layout {
  [[nodiscard]] static auto MaterialDescriptorFileName(
    std::string_view material_name) -> std::string
  {
    return std::string(material_name)
      + std::string(data::kMaterialDescriptorExtension);
  }

  [[nodiscard]] static auto GeometryDescriptorFileName(
    std::string_view geometry_name) -> std::string
  {
    return std::string(geometry_name)
      + std::string(data::kGeometryDescriptorExtension);
  }

  [[nodiscard]] static auto SceneDescriptorFileName(std::string_view scene_name)
    -> std::string
  {
    return std::string(scene_name)
      + std::string(data::kSceneDescriptorExtension);
  }

  [[nodiscard]] static auto PhysicsSceneDescriptorFileName(
    std::string_view scene_name) -> std::string
  {
    return std::string(scene_name)
      + std::string(data::kPhysicsSceneDescriptorExtension);
  }

  [[nodiscard]] static auto TextureDescriptorFileName(std::string_view name)
    -> std::string
  {
    return std::string(name) + std::string(data::kTextureDescriptorExtension);
  }

  [[nodiscard]] static auto BufferDescriptorFileName(std::string_view name)
    -> std::string
  {
    return std::string(name) + std::string(data::kBufferDescriptorExtension);
  }

  [[nodiscard]] static auto PhysicsResourceDescriptorFileName(
    std::string_view name) -> std::string
  {
    return std::string(name)
      + std::string(data::kPhysicsResourceDescriptorExtension);
  }

  [[nodiscard]] auto MaterialVirtualLeaf(std::string_view material_name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kMaterial),
      MaterialDescriptorFileName(material_name));
  }

  [[nodiscard]] auto GeometryVirtualLeaf(std::string_view geometry_name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kGeometry),
      GeometryDescriptorFileName(geometry_name));
  }

  [[nodiscard]] auto SceneVirtualLeaf(std::string_view scene_name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kScene),
      SceneDescriptorFileName(scene_name));
  }

  //! Physics sidecar leaf path for a scene.
  /*!
   Rules:
   - Same descriptor directory as the paired scene descriptor.

   * - Same base scene name as the paired scene descriptor.
   - `.opscene`
   * extension.
  */
  [[nodiscard]] auto PhysicsSceneVirtualLeaf(std::string_view scene_name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kPhysicsScene),
      PhysicsSceneDescriptorFileName(scene_name));
  }

  [[nodiscard]] auto TextureVirtualLeaf(std::string_view name) const
    -> std::string
  {
    return JoinRelPath(TextureDescriptorDir(), TextureDescriptorFileName(name));
  }

  [[nodiscard]] auto BufferVirtualLeaf(std::string_view name) const
    -> std::string
  {
    return JoinRelPath(BufferDescriptorDir(), BufferDescriptorFileName(name));
  }

  [[nodiscard]] auto PhysicsResourceVirtualLeaf(std::string_view name) const
    -> std::string
  {
    return JoinRelPath(
      PhysicsResourcesDir(), PhysicsResourceDescriptorFileName(name));
  }

  [[nodiscard]] auto MaterialDescriptorRelPath(
    std::string_view material_name) const -> std::string
  {
    return MaterialVirtualLeaf(material_name);
  }

  [[nodiscard]] auto GeometryDescriptorRelPath(
    std::string_view geometry_name) const -> std::string
  {
    return GeometryVirtualLeaf(geometry_name);
  }

  [[nodiscard]] auto SceneDescriptorRelPath(std::string_view scene_name) const
    -> std::string
  {
    return SceneVirtualLeaf(scene_name);
  }

  [[nodiscard]] auto PhysicsSceneDescriptorRelPath(
    std::string_view scene_name) const -> std::string
  {
    return PhysicsSceneVirtualLeaf(scene_name);
  }

  [[nodiscard]] auto TextureDescriptorRelPath(std::string_view name) const
    -> std::string
  {
    return TextureVirtualLeaf(name);
  }

  [[nodiscard]] auto BufferDescriptorRelPath(std::string_view name) const
    -> std::string
  {
    return BufferVirtualLeaf(name);
  }

  //! Source-derived sidecar path, including the stable-identity suffix.
  //! Empty stable_id uses name_hint as identity; no files are accessed.
  OXGN_COOK_NDAPI auto TextureDescriptorRelPath(std::string_view name_hint,
    std::string_view stable_id) const -> std::string;

  //! Source-derived buffer sidecar path using the same identity naming rule.
  OXGN_COOK_NDAPI auto BufferDescriptorRelPath(std::string_view name_hint,
    std::string_view stable_id) const -> std::string;

  [[nodiscard]] auto PhysicsResourceDescriptorRelPath(
    std::string_view name) const -> std::string
  {
    return PhysicsResourceVirtualLeaf(name);
  }

  //! Place a descriptor-relative path in this container's virtual namespace.
  [[nodiscard]] auto DescriptorVirtualPath(std::string_view relative_path) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, relative_path);
  }

  [[nodiscard]] auto MaterialVirtualPath(std::string_view material_name) const
    -> std::string
  {
    return JoinVirtualPath(
      virtual_mount_root, MaterialVirtualLeaf(material_name));
  }

  [[nodiscard]] auto GeometryVirtualPath(std::string_view geometry_name) const
    -> std::string
  {
    return JoinVirtualPath(
      virtual_mount_root, GeometryVirtualLeaf(geometry_name));
  }

  [[nodiscard]] auto SceneVirtualPath(std::string_view scene_name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, SceneVirtualLeaf(scene_name));
  }

  [[nodiscard]] auto PhysicsSceneVirtualPath(std::string_view scene_name) const
    -> std::string
  {
    return JoinVirtualPath(
      virtual_mount_root, PhysicsSceneVirtualLeaf(scene_name));
  }

  [[nodiscard]] auto TextureVirtualPath(std::string_view name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, TextureVirtualLeaf(name));
  }

  [[nodiscard]] auto BufferVirtualPath(std::string_view name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, BufferVirtualLeaf(name));
  }

  [[nodiscard]] auto PhysicsResourceVirtualPath(std::string_view name) const
    -> std::string
  {
    return JoinVirtualPath(
      virtual_mount_root, PhysicsResourceVirtualLeaf(name));
  }

  //! Index filename at the cooked-root directory.
  /*!
   This is the file that contains the loose cooked index header, asset entries,
   and file records.

  @note The cooked-root directory is the folder containing this index file.
   Changing this value is intended for tooling pipelines that also control how
   the cooked root is discovered/mounted.

   @warning This must be a filename only (no '/', '\\', or drive letters).
  */
  std::string index_file_name = "container.index.bin";

  //! Base folder (relative to cooked root) for bulk resource blobs.
  /*!
   If empty, resource files are written directly under the cooked root.
  */
  std::string resources_dir = std::string(kResourcesDirName);

  //! File name for the buffers table.
  std::string buffers_table_file_name = "buffers.table";

  //! File name for the buffers data.
  std::string buffers_data_file_name = "buffers.data";

  //! File name for the textures table.
  std::string textures_table_file_name = "textures.table";

  //! File name for the textures data.
  std::string textures_data_file_name = "textures.data";

  //! File name for the physics resource table.
  std::string physics_table_file_name = "physics.table";

  //! File name for the physics resource data.
  std::string physics_data_file_name = "physics.data";

  //! File name for the scripts resource table.
  std::string scripts_table_file_name = "scripts.table";

  //! File name for the scripts resource data.
  std::string scripts_data_file_name = "scripts.data";

  //! Optional base folder (relative to cooked root) for asset descriptors.
  /*!
   If empty, descriptors are written directly under the cooked root.
  */
  std::string descriptors_dir;

  //! Subfolder for scene descriptors.
  /*! Set to empty to place scenes directly under `descriptors_dir`. */
  std::string scenes_subdir = std::string(kScenesDirName);

  //! Subfolder for geometry descriptors.
  /*! Set to empty to place geometry directly under `descriptors_dir`. */
  std::string geometry_subdir = std::string(kGeometryDirName);

  //! Subfolder for material descriptors.
  /*! Set to empty to place materials directly under `descriptors_dir`. */
  std::string materials_subdir = std::string(kMaterialsDirName);

  //! Subfolder for texture resource descriptors (`.otex`).
  /*! Set to empty to place texture descriptors directly under
   *
   * `resources_dir`. */
  std::string texture_descriptors_subdir = "Textures";

  //! Subfolder for buffer resource descriptors (`.obuf`).
  /*! Set to empty to place buffer descriptors directly under
   *
   *
   * `resources_dir`. */
  std::string buffer_descriptors_subdir = "Buffers";

  //! Base folder (relative to cooked root) for physics resources.
  /*! Set to empty to place the physics resource folder under the cooked root.
   */
  std::string physics_dir = "Physics";

  //! Subfolder for physics resource files under `physics_dir`.
  std::string physics_resources_subdir = "Resources";

  //! Resolve the container-relative path for the buffers table.
  [[nodiscard]] auto BuffersTableRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, buffers_table_file_name);
  }

  //! Resolve the container-relative path for the buffers data.
  [[nodiscard]] auto BuffersDataRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, buffers_data_file_name);
  }

  //! Resolve the container-relative path for the textures table.
  [[nodiscard]] auto TexturesTableRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, textures_table_file_name);
  }

  //! Resolve the container-relative path for the textures data.
  [[nodiscard]] auto TexturesDataRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, textures_data_file_name);
  }

  //! Resolve the container-relative path for the physics table.
  [[nodiscard]] auto PhysicsTableRelPath() const -> std::string
  {
    return JoinRelPath(PhysicsResourcesDir(), physics_table_file_name);
  }

  //! Resolve the container-relative path for the physics data.
  [[nodiscard]] auto PhysicsDataRelPath() const -> std::string
  {
    return JoinRelPath(PhysicsResourcesDir(), physics_data_file_name);
  }

  //! Resolve the container-relative path for the scripts table.
  [[nodiscard]] auto ScriptsTableRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, scripts_table_file_name);
  }

  //! Resolve the container-relative path for the scripts data.
  [[nodiscard]] auto ScriptsDataRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, scripts_data_file_name);
  }

  //! Resolve the container-relative directory for texture resource descriptors.
  [[nodiscard]] auto TextureDescriptorDir() const -> std::string
  {
    return JoinRelPath(resources_dir, texture_descriptors_subdir);
  }

  //! Resolve the container-relative directory for buffer resource descriptors.
  [[nodiscard]] auto BufferDescriptorDir() const -> std::string
  {
    return JoinRelPath(resources_dir, buffer_descriptors_subdir);
  }

  //! Resolve the container-relative base directory for physics outputs.
  [[nodiscard]] auto PhysicsRootDir() const -> std::string
  {
    return physics_dir;
  }

  //! Resolve the container-relative directory for physics resources.
  [[nodiscard]] auto PhysicsResourcesDir() const -> std::string
  {
    return JoinRelPath(PhysicsRootDir(), physics_resources_subdir);
  }

  //! Resolve the descriptor folder for an asset type.
  [[nodiscard]] auto DescriptorDirFor(data::AssetType asset_type) const
    -> std::string
  {
    switch (asset_type) {
    case data::AssetType::kScene:
      return JoinRelPath(descriptors_dir, scenes_subdir);
    case data::AssetType::kPhysicsScene:
      return JoinRelPath(descriptors_dir, scenes_subdir);
    case data::AssetType::kGeometry:
      return JoinRelPath(descriptors_dir, geometry_subdir);
    case data::AssetType::kMaterial:
      return JoinRelPath(descriptors_dir, materials_subdir);
    case data::AssetType::kInputAction:
    case data::AssetType::kInputMappingContext:
      // Input sources name their products under their own identity.
    case data::AssetType::kScript:
    case data::AssetType::kPhysicsMaterial:
    case data::AssetType::kCollisionShape:
      // Only single-asset jobs cook these; each is placed at its identity.
    case data::AssetType::kUnknown:
      break;
    }
    return descriptors_dir;
  }

private:
  [[nodiscard]] static auto EnsureLeadingSlash(std::string_view s)
    -> std::string
  {
    if (s.starts_with('/')) {
      return std::string(s);
    }
    return std::string("/") + std::string(s);
  }

  [[nodiscard]] static auto JoinVirtualPath(
    std::string_view root, std::string_view leaf) -> std::string
  {
    const auto u_root = EnsureLeadingSlash(root);
    if (u_root == "/") {
      return EnsureLeadingSlash(leaf);
    }
    if (leaf.empty()) {
      return u_root;
    }
    if (leaf.front() == '/') {
      return u_root + std::string(leaf);
    }
    return u_root + "/" + std::string(leaf);
  }

  [[nodiscard]] static auto JoinRelPath(
    std::string_view base, std::string_view child) -> std::string
  {
    if (base.empty()) {
      return std::string(child);
    }
    if (child.empty()) {
      return std::string(base);
    }
    return std::string(base) + "/" + std::string(child);
  }
};

} // namespace oxygen::content::import
