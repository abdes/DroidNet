//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/MeshOptimization.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! The vertex attributes, beside positions, that simplification preserves.
struct SimplifiedAttributes final {
  bool normals = false;
  bool texcoords = false;
  bool colors = false;
};

//! One generated LOD.
struct GeneratedLod final {
  //! Each surface's triangles, as indices into LOD 0's surface of the same
  //! position.
  std::vector<std::vector<uint32_t>> surface_indices;

  //! The triangles over every surface.
  std::size_t triangle_count = 0;

  //! The largest distance, in the geometry's units, between this LOD and LOD
  //! 0, never smaller than the previous LOD's.
  float error = 0.0F;
};

//! The level that could not reach its reduction, ending the chain.
struct StoppedLevel final {
  //! The level, 1 for the first after LOD 0.
  uint32_t level = 0;
  //! The triangles simplification reached for it.
  std::size_t triangle_count = 0;
  //! The most triangles it could have had to be kept.
  std::size_t allowed_triangle_count = 0;
};

//! The LODs generated after a geometry's LOD 0.
struct GeneratedLods final {
  std::vector<GeneratedLod> lods;

  //! The level that ended the chain before the requested level count, if
  //! one did.
  std::optional<StoppedLevel> stopped;
};

//! Simplifies a geometry's LOD 0 into further LODs.
/*!
 Level `n` simplifies each surface of `lod0` on its own, from LOD 0, toward
 its triangles multiplied by `(1 - reduction)^n`, with its open borders locked
 so neighbouring surfaces do not crack apart, and with `attributes` weighed
 into the collapse cost. A surface that would lose every triangle, or keep
 more than the previous level, keeps the previous level's. A level that removes
 less than half of its reduction from the previous level ends the chain and is
 not kept.
*/
OXGN_COOK_NDAPI auto GenerateLods(std::span<const IndexedSurface> lod0,
  const LodGenerationOptions& options, const SimplifiedAttributes& attributes,
  const std::stop_token& stop_token) -> GeneratedLods;

} // namespace oxygen::content::import::internal
