//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include "MaterialSlotAllocation.h"
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MaterialSlotInventory.h>

namespace oxygen::content::import {
auto AllocateMaterialSlots(const MeshBuildPipeline::WorkItem& item)
  -> MaterialSlotGeometryProvenance
{
  const auto& retained = item.request.material_slot_provenance;
  if (!retained) {
    throw std::invalid_argument(
      "Material slots require retained source provenance");
  }
  const auto geometry_key
    = data::AssetKey::FromVirtualPath(internal::PlaceModelProduct(
      item.request, item.storage_mesh_name, data::kGeometryDescriptorExtension)
        .virtual_path);
  const auto& witness = item.source_layout_witness;
  if (base::IsAllZero(witness)) {
    throw std::invalid_argument(
      "Material slots require an adapter source-layout witness");
  }
  auto result = MaterialSlotGeometryProvenance {
    .source_geometry_anchor = item.storage_mesh_name,
    .source_layout_witness = witness,
    .inventory = {},
    .allocations = {},
  };
  result.inventory.geometry_asset_key = geometry_key;
  for (const auto& lod : item.lods) {
    for (const auto& range : lod.source.ranges) {
      result.allocations.try_emplace(range.source_slot);
    }
  }
  const auto* previous = retained->FindGeometry(result.source_geometry_anchor);
  if (previous != nullptr && previous->source_layout_witness == witness) {
    if (previous->allocations.size() != result.allocations.size()) {
      throw std::invalid_argument(
        "Retained declarations do not match the source witness");
    }
    for (auto& [declaration, id] : result.allocations) {
      const auto found = previous->allocations.find(declaration);
      if (found == previous->allocations.end()) {
        throw std::invalid_argument("Retained slot declaration is missing");
      }
      id = found->second;
    }
    return result;
  }
  const auto source_identity = retained->SourceIdentity().ToString();
  const auto witness_text = fmt::format("{:02x}", fmt::join(witness, ""));
  for (auto& [declaration, id] : result.allocations) {
    // The geometry's place is not part of the slot's identity: moving the
    // model keeps its ids.
    const auto identity = nlohmann::json::array({
      "oxygen.material-slot/v2",
      source_identity,
      result.source_geometry_anchor,
      witness_text,
      declaration,
    });
    id = data::MaterialSlotId::FromStableIdentity(identity.dump());
  }
  return result;
}

} // namespace oxygen::content::import
