//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Scene/Detail/Renderable.h>

using oxygen::scene::detail::Renderable;

/*!
 Behavior:
 - If no GeometryAsset or it has zero LODs -> returns empty.
 - If policy is kFixed -> returns the clamped fixed LOD mesh.
 - If policy is dynamic (kDistance/kScreenSpaceError) and no evaluation has
   been performed yet, returns empty until an evaluation sets a current LOD.

 This avoids exposing raw indices and lets callers work directly with the
 Mesh object (for submeshes, bounds, and draw views).

 @return std::optional with ActiveMesh view; empty if unresolved.
*/
auto Renderable::GetActiveMesh() const noexcept -> std::optional<ActiveMesh>
{
  if (!geometry_asset_)
    return std::nullopt;

  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0)
    return std::nullopt;

  std::size_t lod = 0;
  switch (lod_policy_) {
  case LODPolicy::kFixed: {
    lod = (fixed_lod_index_ < lod_count) ? fixed_lod_index_ : (lod_count - 1);
    break;
  }
  case LODPolicy::kDistance:
  case LODPolicy::kScreenSpaceError: {
    if (!current_lod_.has_value())
      return std::nullopt; // not yet evaluated for current context
    lod = (std::min<std::size_t>)(*current_lod_, lod_count - 1);
    break;
  }
  }

  const auto& mesh_ptr = geometry_asset_->MeshAt(lod);
  if (!mesh_ptr)
    return std::nullopt;

  return ActiveMesh { mesh_ptr, lod };
}
