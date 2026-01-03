//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Content/Import/Layout.h>
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
  folders here (scene/geometry/material). Cameras and lights are components
  attached to scene nodes and are authored into the scene descriptor; they are
  not emitted as standalone loose cooked assets.

 @warning The runtime mount path is rooted at `container.index.bin`.
  Today, mounting assumes this filename at the container root.

 @see oxygen::data::loose_cooked::v1::IndexHeader
 @see oxygen::data::loose_cooked::v1::FileRecord
*/
//! Layout description for loose cooked output.
struct LooseCookedLayout final : Layout {
  //! File extensions for on-disk descriptor files.
  static constexpr std::string_view kMaterialDescriptorExtension = ".omat";
  static constexpr std::string_view kGeometryDescriptorExtension = ".ogeo";
  static constexpr std::string_view kSceneDescriptorExtension = ".oscene";
  static constexpr std::string_view kTextureDescriptorExtension = ".otex";

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
  std::string resources_dir = std::string(Layout::kResourcesDirName);

  //! File name for the buffers table.
  std::string buffers_table_file_name = "buffers.table";

  //! File name for the buffers data.
  std::string buffers_data_file_name = "buffers.data";

  //! File name for the textures table.
  std::string textures_table_file_name = "textures.table";

  //! File name for the textures data.
  std::string textures_data_file_name = "textures.data";

  //! Optional base folder (relative to cooked root) for descriptors.
  /*!
   If empty, descriptors are written directly under the cooked root.
  */
  std::string descriptors_dir;

  //! Subfolder for scene descriptors.
  /*! Set to empty to place scenes directly under `descriptors_dir`. */
  std::string scenes_subdir = std::string(Layout::kScenesDirName);

  //! Subfolder for geometry descriptors.
  /*! Set to empty to place geometry directly under `descriptors_dir`. */
  std::string geometry_subdir = std::string(Layout::kGeometryDirName);

  //! Subfolder for material descriptors.
  /*! Set to empty to place materials directly under `descriptors_dir`. */
  std::string materials_subdir = std::string(Layout::kMaterialsDirName);

  //! Subfolder for texture descriptors.
  /*!
   Texture data is represented as resource table/data files (see
   `textures_table_file_name` and `textures_data_file_name`). It is not a
   loose-cooked "asset descriptor" today because it is not part of
   `oxygen::data::AssetType`.
  */

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

  //! Resolve the descriptor folder for an asset type.
  [[nodiscard]] auto DescriptorDirFor(data::AssetType asset_type) const
    -> std::string
  {
    switch (asset_type) {
    case data::AssetType::kScene:
      return JoinRelPath(descriptors_dir, scenes_subdir);
    case data::AssetType::kGeometry:
      return JoinRelPath(descriptors_dir, geometry_subdir);
    case data::AssetType::kMaterial:
      return JoinRelPath(descriptors_dir, materials_subdir);
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
