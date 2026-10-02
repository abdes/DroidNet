//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>

namespace oxygen::content::import {

//! Resolves exact-witness continuity before optimization. The returned
//! candidate receives its cooked inventory during geometry finalization.
[[nodiscard]] auto AllocateMaterialSlots(
  const MeshBuildPipeline::WorkItem& item) -> MaterialSlotGeometryProvenance;

} // namespace oxygen::content::import
