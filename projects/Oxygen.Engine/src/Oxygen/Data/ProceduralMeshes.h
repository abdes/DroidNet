//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

//! Creates a new Mesh representing a unit axis-aligned cube centered at
//! the origin.
OXGN_DATA_NDAPI auto MakeCubeMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a UV sphere centered at the origin.
OXGN_DATA_NDAPI auto MakeSphereMeshAsset(
  unsigned int latitude_segments = 16, unsigned int longitude_segments = 32)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a flat plane in the XZ plane.
OXGN_DATA_NDAPI auto MakePlaneMeshAsset(
  unsigned int x_segments = 1, unsigned int z_segments = 1, float size = 1.0f)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a cylinder.
OXGN_DATA_NDAPI auto MakeCylinderMeshAsset(
  unsigned int segments = 32, float height = 1.0f, float radius = 0.5f)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a cone.
OXGN_DATA_NDAPI auto MakeConeMeshAsset(
  unsigned int segments = 32, float height = 1.0f, float radius = 0.5f)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a torus.
OXGN_DATA_NDAPI auto MakeTorusMeshAsset(unsigned int major_segments = 32,
  unsigned int minor_segments = 16, float major_radius = 1.0f,
  float minor_radius = 0.25f)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a quad (two triangles in the XY plane).
OXGN_DATA_NDAPI auto MakeQuadMeshAsset(float width = 1.0f, float height = 1.0f)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing an arrow/axis gizmo.
OXGN_DATA_NDAPI auto MakeArrowGizmoMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

OXGN_DATA_NDAPI auto GenerateMeshBuffers(
  std::string_view name, std::span<const std::byte> param_blob)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

OXGN_DATA_NDAPI auto GenerateMesh(std::string_view name,
  std::span<const std::byte> param_blob) -> std::unique_ptr<Mesh>;

} // namespace oxygen::data
