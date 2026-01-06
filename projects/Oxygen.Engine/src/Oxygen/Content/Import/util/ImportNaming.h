//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Naming.h>

namespace oxygen::content::import::util {

//! Builds a stable scene name from an import request.
[[nodiscard]] inline auto BuildSceneName(const ImportRequest& request)
  -> std::string
{
  const auto stem = request.source_path.stem().string();
  if (!stem.empty()) {
    return stem;
  }
  return "Scene";
}

//! Prefixes an imported name with the scene namespace.
[[nodiscard]] inline auto NamespaceImportedAssetName(
  const ImportRequest& request, const std::string_view name) -> std::string
{
  const auto scene_name = BuildSceneName(request);
  if (scene_name.empty()) {
    return std::string(name);
  }
  if (name.empty()) {
    return scene_name;
  }
  return scene_name + "/" + std::string(name);
}

//! Builds an imported material name.
[[nodiscard]] inline auto BuildMaterialName(std::string_view authored,
  const ImportRequest& request, const uint32_t ordinal) -> std::string
{
  if (request.options.naming_strategy) {
    const NamingContext context {
      .kind = ImportNameKind::kMaterial,
      .ordinal = ordinal,
      .parent_name = {},
      .source_id = request.source_path.string(),
    };

    const auto renamed
      = request.options.naming_strategy->Rename(authored, context);
    if (renamed.has_value() && !renamed->empty()) {
      return *renamed;
    }
  }

  if (!authored.empty()) {
    return std::string(authored);
  }

  return "M_Material_" + std::to_string(ordinal);
}

//! Builds an imported mesh name.
[[nodiscard]] inline auto BuildMeshName(std::string_view authored,
  const ImportRequest& request, const uint32_t ordinal) -> std::string
{
  if (request.options.naming_strategy) {
    const NamingContext context {
      .kind = ImportNameKind::kMesh,
      .ordinal = ordinal,
      .parent_name = {},
      .source_id = request.source_path.string(),
    };

    const auto renamed
      = request.options.naming_strategy->Rename(authored, context);
    if (renamed.has_value() && !renamed->empty()) {
      return *renamed;
    }
  }

  if (!authored.empty()) {
    return std::string(authored);
  }

  return "G_Mesh_" + std::to_string(ordinal);
}

//! Builds an imported scene node name.
[[nodiscard]] inline auto BuildSceneNodeName(std::string_view authored,
  const ImportRequest& request, const uint32_t ordinal,
  std::string_view parent_name) -> std::string
{
  if (request.options.naming_strategy) {
    const NamingContext context {
      .kind = ImportNameKind::kSceneNode,
      .ordinal = ordinal,
      .parent_name = parent_name,
      .source_id = request.source_path.string(),
    };

    const auto renamed
      = request.options.naming_strategy->Rename(authored, context);
    if (renamed.has_value() && !renamed->empty()) {
      return *renamed;
    }
  }

  if (!authored.empty()) {
    return std::string(authored);
  }

  return "N_Node_" + std::to_string(ordinal);
}

} // namespace oxygen::content::import::util
