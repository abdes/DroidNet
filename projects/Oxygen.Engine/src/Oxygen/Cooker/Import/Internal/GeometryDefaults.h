//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::content::import::internal {

//! The authored asset defaults of one surface; an absent value takes the scene
//! schema's default.
struct AuthoredSurfaceDefaults final {
  data::SurfaceId surface_id;
  std::optional<bool> visible;
  std::optional<bool> casts_shadows;
};

//! The surface table of a geometry declaring `declared`, in that order.
/*!
 Each value is the authored one when `authored` lists the surface, else the
 scene schema's default. `authored` lists only declared surfaces, each once.
*/
OXGN_COOK_NDAPI auto MakeSurfaceTable(std::span<const data::SurfaceId> declared,
  std::span<const AuthoredSurfaceDefaults> authored)
  -> std::vector<data::pak::geometry::SurfaceDesc>;

//! The authored default LOD policy, else the scene schema's default.
OXGN_COOK_NDAPI auto MakeDefaultLodPolicy(
  const std::optional<data::pak::geometry::LodPolicyRecord>& authored)
  -> data::pak::geometry::LodPolicyRecord;

//! The default LOD policy of an imported model's geometry with `lod_count`
//! LODs.
/*!
 One LOD keeps the scene schema's default. Several LODs select by screen-space
 error, so an application that sets no policy still sees them switch: boundary
 `i` enters its finer LOD at a projected radius of 256 / 2^i pixels and its
 coarser LOD at 0.8 of that, a band that keeps the LOD from flickering.
*/
OXGN_COOK_NDAPI auto MakeImportedLodPolicy(std::size_t lod_count)
  -> data::pak::geometry::LodPolicyRecord;

//! The LOD policy `document` spells, as a descriptor's `lod_policy` object.
/*!
 `document` has passed the descriptor schema's `lod_policy` definition. Returns
 nullopt when the values break the rules the schema cannot state: thresholds
 that decrease, or screen-space error lists of different lengths.
*/
OXGN_COOK_NDAPI auto ReadLodPolicy(const nlohmann::json& document)
  -> std::optional<data::pak::geometry::LodPolicyRecord>;

//! The `lod_policy` object that spells `record`, as ReadLodPolicy reads it.
OXGN_COOK_NDAPI auto WriteLodPolicy(
  const data::pak::geometry::LodPolicyRecord& record) -> nlohmann::json;

} // namespace oxygen::content::import::internal
