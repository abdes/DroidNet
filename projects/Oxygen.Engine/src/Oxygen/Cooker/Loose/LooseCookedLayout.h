//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

#include <Oxygen/Content/Layout.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import {

//! Configurable relative paths for a loose cooked container.
/*!
 The runtime Content loader expects a `container.index.bin` index at the root
 plus optional resource table/data files referenced by that index.

 These are conventions used by Oxygen tooling; the loader ultimately follows
 whatever paths are recorded in the index.

 ### Layout Goals

 - Allow tooling to group descriptor files by asset type
   (scene/geometry/materials) or to put all descriptors into a single folder.
 - Keep resource blob files (tables/data) under a dedicated resources folder.
 - Keep a virtual mount root that anchors virtual paths recorded in the index.
 - Ensure all configured *physical* paths are container-relative and use
   forward slashes.

 @note Only Oxygen asset types (`oxygen::data::AssetType`) have descriptor


 folders here (scene/geometry/material/script/input). Cameras and lights are

 components attached to scene nodes and are authored into the scene
 descriptor;
 they are not emitted as standalone loose cooked assets.

 @warning The runtime mount path is rooted at `container.index.bin`.
  Today, mounting assumes this filename at the container root.

 @see oxygen::data::loose_cooked::IndexHeader
 @see oxygen::data::loose_cooked::FileRecord
*/
//! Layout description for loose cooked output.
struct LooseCookedLayout final : Layout {
  //! File extensions for on-disk descriptor files.
  static constexpr std::string_view kMaterialDescriptorExtension = ".omat";
  static constexpr std::string_view kGeometryDescriptorExtension = ".ogeo";
  static constexpr std::string_view kSceneDescriptorExtension = ".oscene";
  //! Physics sidecar descriptor extension. Must be colocated with `.oscene`.
  static constexpr std::string_view kPhysicsSceneDescriptorExtension
    = ".physics";
  static constexpr std::string_view kScriptDescriptorExtension = ".oscript";
  static constexpr std::string_view kInputActionDescriptorExtension = ".oiact";
  static constexpr std::string_view kInputMappingContextDescriptorExtension
    = ".oimap";
  static constexpr std::string_view kTextureDescriptorExtension = ".otex";
  static constexpr std::string_view kBufferDescriptorExtension = ".obuf";

  [[nodiscard]] static auto MaterialDescriptorFileName(
    std::string_view material_name) -> std::string
  {
    return std::string(material_name)
      + std::string(kMaterialDescriptorExtension);
  }

  [[nodiscard]] static auto GeometryDescriptorFileName(
    std::string_view geometry_name) -> std::string
  {
    return std::string(geometry_name)
      + std::string(kGeometryDescriptorExtension);
  }

  [[nodiscard]] static auto SceneDescriptorFileName(std::string_view scene_name)
    -> std::string
  {
    return std::string(scene_name) + std::string(kSceneDescriptorExtension);
  }

  [[nodiscard]] static auto PhysicsSceneDescriptorFileName(
    std::string_view scene_name) -> std::string
  {
    return std::string(scene_name)
      + std::string(kPhysicsSceneDescriptorExtension);
  }

  [[nodiscard]] static auto ScriptDescriptorFileName(std::string_view name)
    -> std::string
  {
    return std::string(name) + std::string(kScriptDescriptorExtension);
  }

  [[nodiscard]] static auto InputActionDescriptorFileName(std::string_view name)
    -> std::string
  {
    return std::string(name) + std::string(kInputActionDescriptorExtension);
  }

  [[nodiscard]] static auto InputMappingContextDescriptorFileName(
    std::string_view name) -> std::string
  {
    return std::string(name)
      + std::string(kInputMappingContextDescriptorExtension);
  }

  [[nodiscard]] static auto TextureDescriptorFileName(std::string_view name)
    -> std::string
  {
    return std::string(name) + std::string(kTextureDescriptorExtension);
  }

  [[nodiscard]] static auto BufferDescriptorFileName(std::string_view name)
    -> std::string
  {
    return std::string(name) + std::string(kBufferDescriptorExtension);
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
   - `.physics`
   * extension.
  */
  [[nodiscard]] auto PhysicsSceneVirtualLeaf(std::string_view scene_name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kPhysicsScene),
      PhysicsSceneDescriptorFileName(scene_name));
  }

  [[nodiscard]] auto ScriptVirtualLeaf(std::string_view name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kScript),
      ScriptDescriptorFileName(name));
  }

  [[nodiscard]] auto InputActionVirtualLeaf(std::string_view name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kInputAction),
      InputActionDescriptorFileName(name));
  }

  [[nodiscard]] auto InputMappingContextVirtualLeaf(std::string_view name) const
    -> std::string
  {
    return JoinRelPath(DescriptorDirFor(data::AssetType::kInputMappingContext),
      InputMappingContextDescriptorFileName(name));
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

  [[nodiscard]] auto ScriptDescriptorRelPath(std::string_view name) const
    -> std::string
  {
    return ScriptVirtualLeaf(name);
  }

  [[nodiscard]] auto InputActionDescriptorRelPath(std::string_view name) const
    -> std::string
  {
    return InputActionVirtualLeaf(name);
  }

  [[nodiscard]] auto InputMappingContextDescriptorRelPath(
    std::string_view name) const -> std::string
  {
    return InputMappingContextVirtualLeaf(name);
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

  [[nodiscard]] auto ScriptVirtualPath(std::string_view name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, ScriptVirtualLeaf(name));
  }

  [[nodiscard]] auto InputActionVirtualPath(std::string_view name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, InputActionVirtualLeaf(name));
  }

  [[nodiscard]] auto InputMappingContextVirtualPath(std::string_view name) const
    -> std::string
  {
    return JoinVirtualPath(
      virtual_mount_root, InputMappingContextVirtualLeaf(name));
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

  //! File name for the script-bindings table.
  std::string script_bindings_table_file_name = "script-bindings.table";

  //! File name for the script-bindings data.
  std::string script_bindings_data_file_name = "script-bindings.data";

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

  //! Subfolder for script descriptors.
  /*! Set to empty to place script descriptors directly under `descriptors_dir`.
   */
  std::string scripts_subdir = "Scripts";

  //! Subfolder for all input descriptors (`.oiact`, `.oimap`).
  /*! Set to empty to place input descriptors directly under
   *
   * `descriptors_dir`. */
  std::string input_subdir = "Input";

  //! Subfolder for texture resource descriptors (`.otex`).
  /*! Set to empty to place texture descriptors directly under
   *
   * `resources_dir`. */
  std::string texture_descriptors_subdir = "Textures";

  //! Subfolder for buffer resource descriptors (`.obuf`).
  /*! Set to empty to place buffer descriptors directly under
   *
   * `resources_dir`. */
  std::string buffer_descriptors_subdir = "Buffers";

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
    return JoinRelPath(resources_dir, physics_table_file_name);
  }

  //! Resolve the container-relative path for the physics data.
  [[nodiscard]] auto PhysicsDataRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, physics_data_file_name);
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

  //! Resolve the container-relative path for the script-bindings table.
  [[nodiscard]] auto ScriptBindingsTableRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, script_bindings_table_file_name);
  }

  //! Resolve the container-relative path for the script-bindings data.
  [[nodiscard]] auto ScriptBindingsDataRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, script_bindings_data_file_name);
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

  //! Resolve the descriptor folder for an asset type.
  [[nodiscard]] auto DescriptorDirFor(data::AssetType asset_type) const
    -> std::string
  {
    switch (asset_type) {
    case data::AssetType::kScene:
      return JoinRelPath(descriptors_dir, scenes_subdir);
    case data::AssetType::kPhysicsScene:
      // Physics sidecars are scene companions and must be colocated with
      // `.oscene` descriptors.
      return JoinRelPath(descriptors_dir, scenes_subdir);
    case data::AssetType::kGeometry:
      return JoinRelPath(descriptors_dir, geometry_subdir);
    case data::AssetType::kMaterial:
      return JoinRelPath(descriptors_dir, materials_subdir);
    case data::AssetType::kScript:
      return JoinRelPath(descriptors_dir, scripts_subdir);
    case data::AssetType::kInputAction:
      return JoinRelPath(descriptors_dir, input_subdir);
    case data::AssetType::kInputMappingContext:
      return JoinRelPath(descriptors_dir, input_subdir);
    case data::AssetType::kUnknown:
      break;
    case data::AssetType::kPhysicsMaterial:
    case data::AssetType::kCollisionShape:
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
