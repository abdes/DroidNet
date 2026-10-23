//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::vortex::testing {

//! The surface bound to the first submesh of the node's finest LOD.
inline auto FirstSurfaceOf(scene::SceneNode& node) -> data::SurfaceId
{
  const auto geometry = node.GetRenderable().GetGeometry();
  CHECK_F(geometry != nullptr, "the node has no geometry");
  const auto& mesh = geometry->MeshAt(data::LodIndex { 0 });
  CHECK_F(mesh != nullptr, "the geometry has no LOD 0");
  CHECK_F(!mesh->SubMeshes().empty(), "the LOD 0 mesh has no submesh");
  return mesh->SubMeshes().front().SurfaceIdentity();
}

} // namespace oxygen::vortex::testing
