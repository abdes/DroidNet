//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include "SurfaceAllocation.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>

namespace oxygen::content::import {
auto AllocateSurfaces(const MeshBuildPipeline::WorkItem& item)
  -> SurfaceGeometryProvenance
{
  const auto& retained = item.request.surface_provenance;
  if (!retained) {
    throw std::invalid_argument("Surfaces require recorded source provenance");
  }
  const auto geometry_key
    = data::AssetKey::FromVirtualPath(internal::PlaceModelProduct(
      item.request, item.storage_mesh_name, data::kGeometryDescriptorExtension)
        .virtual_path);
  const auto& witness = item.source_layout_witness;
  if (base::IsAllZero(witness)) {
    throw std::invalid_argument(
      "Surfaces require an adapter source-layout witness");
  }
  auto result = SurfaceGeometryProvenance {
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
        "Recorded declarations do not match the source witness");
    }
    for (auto& [declaration, id] : result.allocations) {
      const auto found = previous->allocations.find(declaration);
      if (found == previous->allocations.end()) {
        throw std::invalid_argument("Recorded surface declaration is missing");
      }
      id = found->second;
    }
    return result;
  }
  const auto source_identity = retained->SourceIdentity().ToString();
  const auto witness_text = base::ToHexString(witness);
  for (auto& [declaration, id] : result.allocations) {
    // The geometry's place is not part of the surface's identity: moving the
    // model keeps its ids.
    const auto identity = nlohmann::json::array({
      // The tag keeps its original name: it is a hash input, and changing it
      // would
      // change every allocated surface id.
      "oxygen.material-slot/v2",
      source_identity,
      result.source_geometry_anchor,
      witness_text,
      declaration,
    });
    id = data::SurfaceId::FromStableIdentity(identity.dump());
  }
  return result;
}

} // namespace oxygen::content::import
