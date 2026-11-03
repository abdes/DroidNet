//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import::internal {

//! The existing materials that replace source materials, by the source
//! material's name in the import.
using MaterialRemapKeys = std::map<std::string, data::AssetKey, std::less<>>;

//! Resolve the import's material remap in the cooked roots the job reads.
/*!
 Each target is a reference read in the import's settings: absolute in another
 mount, or relative to the folder of the model's identity. A target the
 grammar refuses (`import.reference_invalid`) or that is in no mounted cooked
 root (`import.reference_unmounted`) is an error. A target that names no
 existing material is reported (`import.material_remap_target_missing`, a
 warning), and its source material is produced instead.
*/
[[nodiscard]] inline auto ResolveMaterialRemap(
  ImportSession& session, const ImportRequest& request) -> MaterialRemapKeys
{
  auto keys = MaterialRemapKeys {};
  const auto& remap = request.options.material_remap;
  if (remap.empty()) {
    return keys;
  }
  const auto mounts
    = LoadMountedCookedSources(session, request, "import.material_remap.");
  for (const auto& [name, target] : remap) {
    const auto object_path = "material_remap/" + name;
    const auto reference = ResolveJobReference(request, target);
    if (!reference.has_value()) {
      auto refused
        = RefuseReference(request, target, reference.error(), std::string {});
      refused.object_path = object_path;
      session.AddDiagnostic(std::move(refused));
      continue;
    }
    const auto found = LookupReference(
      mounts, request, *reference, data::AssetType::kMaterial);
    if (found.has_value()) {
      keys.emplace(name, found->key);
      continue;
    }
    if (found.error().reason == AssetReferenceRefusal::kUnmounted) {
      auto refused
        = RefuseUnmountedReference(request, *reference, std::string {});
      refused.object_path = object_path;
      session.AddDiagnostic(std::move(refused));
      continue;
    }
    session.AddDiagnostic({
      .severity = ImportSeverity::kWarning,
      .code = "import.material_remap_target_missing",
      .message = DescribeLookup(*reference, found.error())
        + "; it does not exist or is not a material, so the source material '"
        + name + "' is produced instead",
      .source_path = request.source_path.string(),
      .object_path = object_path,
    });
  }
  return keys;
}

//! The materials of a model import: those it produces and those a
//! `material_remap` replaces.
struct ModelMaterials final {
  std::span<const MaterialSource* const> produced;
  std::span<const MaterialSource* const> replaced;
};

//! The texture source IDs that only replaced materials bind, which the import
//! does not produce.
[[nodiscard]] inline auto TexturesOnlyReplacedMaterialsUse(
  const ModelMaterials& materials) -> std::set<std::string, std::less<>>
{
  const auto& [produced, replaced] = materials;
  const auto collect = [](std::span<const MaterialSource* const> materials) {
    auto ids = std::set<std::string, std::less<>> {};
    for (const auto* material : materials) {
      for (const auto& slot : MaterialSource::TextureSlots()) {
        const auto& binding = material->textures.*slot.binding;
        if (binding.assigned && !binding.source_id.empty()) {
          ids.insert(binding.source_id);
        }
      }
    }
    return ids;
  };
  auto dropped = collect(replaced);
  for (const auto& id : collect(produced)) {
    dropped.erase(id);
  }
  return dropped;
}

//! The names a model import gives its products, which keyed settings name.
struct ModelProductNames final {
  std::set<std::string, std::less<>> materials;
  std::set<std::string, std::less<>> textures;
  std::set<std::string, std::less<>> geometries;
};

//! Report each keyed setting whose key names no product of the model.
/*!
 The setting is kept, inert, and reported (`import.setting_target_missing`, a
 warning), as a kept change is. A surface key is checked by the mesh build of
 its geometry.
*/
[[nodiscard]] inline auto ReportMissingSettingTargets(
  const ImportOptions& options, const ModelProductNames& names,
  std::string_view source_id) -> std::vector<ImportDiagnostic>
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto check
    = [&](const std::string_view setting, const std::string& key,
        const std::set<std::string, std::less<>>& products) -> void {
    if (products.contains(key)) {
      return;
    }
    diagnostics.push_back({
      .severity = ImportSeverity::kWarning,
      .code = "import.setting_target_missing",
      .message = std::string(setting) + " names '" + key
        + "', which the source no longer has; the setting is kept and not "
          "applied",
      .source_path = std::string(source_id),
      .object_path = std::string(setting) + "/" + key,
    });
  };
  // Products the import does not produce cannot be checked.
  const auto imports = [&](const ImportContentFlags flag) -> bool {
    return (options.import_content & flag) != ImportContentFlags::kNone;
  };
  if (imports(ImportContentFlags::kMaterials)) {
    for (const auto& entry : options.material_remap) {
      check("material_remap", entry.first, names.materials);
    }
  }
  if (imports(ImportContentFlags::kTextures)) {
    for (const auto& entry : options.texture_overrides) {
      check("texture_overrides", entry.first, names.textures);
    }
  }
  if (imports(ImportContentFlags::kGeometry)) {
    for (const auto& entry : options.geometry_defaults) {
      check("geometry_defaults", entry.first, names.geometries);
    }
  }
  return diagnostics;
}

} // namespace oxygen::content::import::internal
