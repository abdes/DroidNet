//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SurfaceAllocation.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>

namespace oxygen::content::import {
namespace {

  constexpr auto kUnnamedMaterial = std::string_view { "#unnamed" };

  //! The declaration key of a slot's source material, before any numbering. An
  //! escaped name never starts with `#`, so no name reads as either constant.
  auto DeclarationBase(const std::optional<std::string>& material)
    -> std::string
  {
    if (!material.has_value()) {
      return "#unassigned";
    }
    if (material->empty()) {
      return std::string(kUnnamedMaterial);
    }
    return internal::EscapeAnchorName(*material);
  }

  //! The declaration key of each slot the geometry's LODs use, numbering a
  //! declaration that repeats in slot order.
  auto DeclarationKeys(const MeshBuildPipeline::WorkItem& item)
    -> std::map<uint32_t, std::string>
  {
    auto keys = std::map<uint32_t, std::string> {};
    for (const auto& lod : item.lods) {
      for (const auto& range : lod.source.ranges) {
        keys.try_emplace(range.source_slot);
      }
    }
    auto counts = std::map<std::string, uint32_t> {};
    for (auto& [slot, key] : keys) {
      const auto material = item.surface_materials.find(slot);
      if (material == item.surface_materials.end()) {
        throw std::invalid_argument("Surface slot " + std::to_string(slot)
          + " declares no source material");
      }
      const auto base = DeclarationBase(material->second);
      const auto count = ++counts[base];
      const auto known_by_place = base == kUnnamedMaterial;
      key = count > 1U || known_by_place
        ? base + "[" + std::to_string(count) + "]"
        : base;
    }
    return keys;
  }

  //! The ID of a declaration seen for the first time: the same for the same
  //! key, unless a surface of the geometry already holds it, as one renamed
  //! from that key does.
  auto DeriveSurfaceId(const std::string& source_identity,
    const std::string& anchor, const std::string& key,
    const std::set<data::SurfaceId>& taken) -> data::SurfaceId
  {
    for (uint32_t attempt = 0;; ++attempt) {
      // The tag is a hash input: changing it changes every new surface ID.
      auto identity = nlohmann::json::array({
        "oxygen.model-surface/v1",
        source_identity,
        anchor,
        key,
      });
      if (attempt > 0U) {
        identity.push_back(attempt);
      }
      const auto id = data::SurfaceId::FromStableIdentity(identity.dump());
      if (!taken.contains(id)) {
        return id;
      }
    }
  }

  auto Quoted(const std::set<std::string>& keys) -> std::string
  {
    auto text = std::string {};
    for (const auto& key : keys) {
      text += (text.empty() ? "'" : ", '") + key + "'";
    }
    return text;
  }

} // namespace

auto AllocateSurfaces(const MeshBuildPipeline::WorkItem& item,
  std::vector<ImportDiagnostic>& diagnostics) -> AllocatedSurfaces
{
  const auto& retained = item.request.surface_provenance;
  if (!retained) {
    throw std::invalid_argument("Surfaces require recorded source provenance");
  }
  auto result = AllocatedSurfaces {};
  auto& geometry = result.provenance;
  geometry.source_geometry_anchor = item.storage_mesh_name;
  geometry.inventory.geometry_asset_key
    = data::AssetKey::FromVirtualPath(internal::PlaceModelProduct(
      item.request, item.storage_mesh_name, data::kGeometryDescriptorExtension)
        .virtual_path);

  const auto keys = DeclarationKeys(item);
  const auto* previous
    = retained->FindGeometry(geometry.source_geometry_anchor);
  auto vanished = previous != nullptr
    ? previous->allocations
    : std::map<std::string, data::SurfaceId> {};
  auto taken = std::set<data::SurfaceId> {};
  auto added = std::set<std::string> {};
  for (const auto& [slot, key] : keys) {
    const auto kept = vanished.find(key);
    if (kept == vanished.end()) {
      added.insert(key);
      continue;
    }
    geometry.allocations.emplace(key, kept->second);
    taken.insert(kept->second);
    vanished.erase(kept);
  }

  // One declaration gone and one new is a rename, such as a DCC's cleanup of
  // `Material.001` to `Paint`. With more of either, nothing says which is
  // which.
  auto renamed = std::string {};
  if (added.size() == 1U && vanished.size() == 1U) {
    const auto& [old_key, id] = *vanished.begin();
    const auto& new_key = *added.begin();
    renamed = "'" + old_key + "' to '" + new_key + "'";
    geometry.allocations.emplace(new_key, id);
    added.clear();
    vanished.clear();
  } else {
    const auto source_identity = retained->SourceIdentity().ToString();
    for (const auto& key : added) {
      const auto id = DeriveSurfaceId(
        source_identity, geometry.source_geometry_anchor, key, taken);
      geometry.allocations.emplace(key, id);
      taken.insert(id);
    }
  }
  for (const auto& [slot, key] : keys) {
    result.ids_by_slot.emplace(slot, geometry.allocations.at(key));
  }

  auto vanished_keys = std::set<std::string> {};
  for (const auto& [key, id] : vanished) {
    vanished_keys.insert(key);
  }
  if (previous != nullptr
    && (!renamed.empty() || !added.empty() || !vanished_keys.empty())) {
    auto message = "The surfaces of the geometry '"
      + geometry.source_geometry_anchor + "' changed since its last import.";
    if (!renamed.empty()) {
      message += " Renamed, keeping its ID: " + renamed + ".";
    }
    if (!vanished_keys.empty()) {
      message += " Vanished: " + Quoted(vanished_keys) + ".";
    }
    if (!added.empty()) {
      message += " New: " + Quoted(added) + ".";
    }
    diagnostics.push_back({
      .severity = ImportSeverity::kInfo,
      .code = std::string(import_codes::kSurfacesChanged),
      .message = std::move(message),
      .source_path = item.source_id,
      .object_path = item.mesh_name,
    });
  }
  return result;
}

} // namespace oxygen::content::import
