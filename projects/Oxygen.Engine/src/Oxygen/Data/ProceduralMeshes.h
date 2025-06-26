//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Data/MeshAsset.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Creates a new MeshAsset representing a unit axis-aligned cube centered at
//! the origin.
OXGN_DATA_NDAPI auto MakeCubeMeshAsset() -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing a UV sphere centered at the origin.
OXGN_DATA_NDAPI auto MakeSphereMeshAsset(unsigned int latitude_segments = 16,
  unsigned int longitude_segments = 32) -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing a flat plane in the XZ plane.
OXGN_DATA_NDAPI auto MakePlaneMeshAsset(unsigned int x_segments = 1,
  unsigned int z_segments = 1, float size = 1.0f) -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing a cylinder.
OXGN_DATA_NDAPI auto MakeCylinderMeshAsset(unsigned int segments = 32,
  float height = 1.0f, float radius = 0.5f) -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing a cone.
OXGN_DATA_NDAPI auto MakeConeMeshAsset(unsigned int segments = 32,
  float height = 1.0f, float radius = 0.5f) -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing a torus.
OXGN_DATA_NDAPI auto MakeTorusMeshAsset(unsigned int major_segments = 32,
  unsigned int minor_segments = 16, float major_radius = 1.0f,
  float minor_radius = 0.25f) -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing a quad (two triangles in the XY plane).
OXGN_DATA_NDAPI auto MakeQuadMeshAsset(float width = 1.0f, float height = 1.0f)
  -> std::shared_ptr<MeshAsset>;

//! Creates a new MeshAsset representing an arrow/axis gizmo.
OXGN_DATA_NDAPI auto MakeArrowGizmoMeshAsset() -> std::shared_ptr<MeshAsset>;

} // namespace oxygen::data
