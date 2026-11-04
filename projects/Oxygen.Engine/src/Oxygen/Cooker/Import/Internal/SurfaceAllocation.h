//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::content::import {

//! The surfaces of one geometry of an import.
struct AllocatedSurfaces final {
  //! What the import records; its inventory is filled at finalization.
  SurfaceGeometryProvenance provenance;
  //! The ID of each source slot the geometry's LODs use.
  std::map<uint32_t, data::SurfaceId> ids_by_slot;
};

//! Gives each surface of a geometry its ID, before optimization.
/*!
 A surface is known by its declaration key: the name of its source material
 (`Paint`), the same name numbered in slot order (`Paint[2]`), `#unnamed[n]` for
 a material without a name and `#unassigned` for no material. So editing
 vertices and reordering slots or materials keep every ID.

 A key the geometry's recorded provenance holds keeps its ID. If exactly one key
 vanished and exactly one appeared, the new one is a rename and keeps the ID.
 Any other new key derives its ID from the source identity, the geometry's
 anchor and the key, so a key that returns has the ID it had, unless a surface
 renamed from it holds that ID.

 Against recorded provenance, reports what changed as `import.surfaces_changed`
 (info). Throws std::invalid_argument for a slot in use that declares no source
 material.
*/
[[nodiscard]] auto AllocateSurfaces(const MeshBuildPipeline::WorkItem& item,
  std::vector<ImportDiagnostic>& diagnostics) -> AllocatedSurfaces;

} // namespace oxygen::content::import
