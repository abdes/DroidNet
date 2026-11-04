//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>

namespace oxygen::content::import::test {

//! Declares a source material for each source slot of `item`'s ranges, as an
//! adapter does: `Material<slot>`.
inline auto DeclareFixtureSurfaces(MeshBuildPipeline::WorkItem& item) -> void
{
  for (const auto& lod : item.lods) {
    for (const auto& range : lod.source.ranges) {
      item.surface_materials.try_emplace(
        range.source_slot, "Material" + std::to_string(range.source_slot));
    }
  }
}

} // namespace oxygen::content::import::test
