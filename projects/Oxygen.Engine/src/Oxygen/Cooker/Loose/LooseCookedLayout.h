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

namespace oxygen::content::import {

//! Configurable relative paths for a loose cooked container.
/*!
 The runtime Content loader expects a `container.index.bin` index at the root
 plus optional resource table/data files referenced by that index. The loader
 follows whatever paths the index records.

 ### What the layout places

 The cooked layout is the identity: every descriptor lives at the path of its
 virtual path under the cooked root, so the descriptor of `/Mount/X/Y.ext` is
 `X/Y.ext`. A container's products are named under its identity: an input
 source's actions (`X/Player.input/Jump.oiact`) and a model's materials,
 geometry, textures, scene and buffer files (`X/Ship.glb/Paint.omat`). The
 layout has no folder for any of them.

 It places the shared resource tables, and anchors virtual paths at the
 virtual mount root.

 - Keep resource blob files (tables/data) under a dedicated resources folder.
 - Keep a virtual mount root that anchors virtual paths recorded in the index.
 - Ensure all configured *physical* paths are container-relative and use
   forward slashes.

 @warning The runtime mount path is rooted at `container.index.bin`.
  Today, mounting assumes this filename at the container root.

 @see oxygen::data::loose_cooked::IndexHeader
 @see oxygen::data::loose_cooked::FileRecord
*/
struct LooseCookedLayout final : Layout {
  //! Place a descriptor-relative path in this container's virtual namespace.
  [[nodiscard]] auto DescriptorVirtualPath(std::string_view relative_path) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root, relative_path);
  }

  //! Index filename at the cooked-root directory.
  /*!
   This is the file that contains the loose cooked index header, asset entries,
   and file records.

  @note The cooked-root directory is the folder containing this index file.
   Changing this value is intended for tooling pipelines that also control how
   the cooked root is discovered/mounted.

   @warning This must be a filename only (no '/', '\', or drive letters).
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

  //! Resolve the container-relative directory for physics resources.
  [[nodiscard]] auto PhysicsResourcesDir() const -> std::string
  {
    return JoinRelPath(physics_dir, physics_resources_subdir);
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
