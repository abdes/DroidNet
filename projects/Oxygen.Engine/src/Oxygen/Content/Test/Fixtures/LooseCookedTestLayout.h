//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace oxygen::content::testing {

//! Test-only loose cooked layout helper for Content runtime fixtures.
struct LooseCookedLayout {
  static constexpr std::string_view kMaterialDescriptorExtension = ".omat";
  static constexpr std::string_view kGeometryDescriptorExtension = ".ogeo";
  static constexpr std::string_view kSceneDescriptorExtension = ".oscene";
  static constexpr std::string_view kPhysicsSceneDescriptorExtension
    = ".physics";

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

  [[nodiscard]] auto MaterialVirtualPath(std::string_view material_name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root,
      JoinRelPath(materials_subdir, MaterialDescriptorFileName(material_name)));
  }

  [[nodiscard]] auto SceneVirtualPath(std::string_view scene_name) const
    -> std::string
  {
    return JoinVirtualPath(virtual_mount_root,
      JoinRelPath(scenes_subdir, SceneDescriptorFileName(scene_name)));
  }

  [[nodiscard]] auto TexturesTableRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, textures_table_file_name);
  }

  [[nodiscard]] auto TexturesDataRelPath() const -> std::string
  {
    return JoinRelPath(resources_dir, textures_data_file_name);
  }

  std::string virtual_mount_root = "/.cooked";
  std::string index_file_name = "container.index.bin";
  std::string resources_dir = "Resources";
  std::string buffers_table_file_name = "buffers.table";
  std::string buffers_data_file_name = "buffers.data";
  std::string textures_table_file_name = "textures.table";
  std::string textures_data_file_name = "textures.data";
  std::string physics_table_file_name = "physics.table";
  std::string physics_data_file_name = "physics.data";
  std::string scenes_subdir = "Scenes";
  std::string geometry_subdir = "Geometry";
  std::string materials_subdir = "Materials";

private:
  [[nodiscard]] static auto EnsureLeadingSlash(std::string_view s)
    -> std::string
  {
    if (s.starts_with('/')) {
      return std::string(s);
    }
    return std::string("/") + std::string(s);
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

  [[nodiscard]] static auto JoinVirtualPath(
    std::string_view root, std::string_view leaf) -> std::string
  {
    const auto normalized_root = EnsureLeadingSlash(root);
    if (normalized_root == "/") {
      return EnsureLeadingSlash(leaf);
    }
    if (leaf.empty()) {
      return normalized_root;
    }
    if (leaf.front() == '/') {
      return normalized_root + std::string(leaf);
    }
    return normalized_root + "/" + std::string(leaf);
  }
};

} // namespace oxygen::content::testing
