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
#include <Oxygen/Data/ProceduralMeshDefaults.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

//! Creates a new Mesh representing a unit axis-aligned cube centered at
//! the origin.
OXGN_DATA_NDAPI auto MakeCubeMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a unit cube surface with welded vertices and configurable
//! subdivisions per face edge.
OXGN_DATA_NDAPI auto MakeSubdividedCubeMeshAsset(
  unsigned int segments = procedural::kSubdividedCubeSegments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a UV sphere centered at the origin.
OXGN_DATA_NDAPI auto MakeSphereMeshAsset(
  unsigned int latitude_segments = procedural::kSphereLatitudeSegments,
  unsigned int longitude_segments = procedural::kSphereLongitudeSegments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new geodesic/icosphere mesh centered at the origin.
OXGN_DATA_NDAPI auto MakeIcoSphereMeshAsset(
  unsigned int subdivision_level = procedural::kIcoSphereSubdivisionLevel)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Alias of MakeIcoSphereMeshAsset for authoring/runtime readability.
OXGN_DATA_NDAPI auto MakeGeodesicSphereMeshAsset(
  unsigned int subdivision_level = procedural::kIcoSphereSubdivisionLevel)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a flat plane in the XY plane at `z = 0`.
OXGN_DATA_NDAPI auto MakePlaneMeshAsset(
  unsigned int x_segments = procedural::kPlaneXSegments,
  unsigned int z_segments = procedural::kPlaneZSegments,
  float size = procedural::kPlaneSize)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a cylinder.
OXGN_DATA_NDAPI auto MakeCylinderMeshAsset(
  unsigned int segments = procedural::kCylinderSegments,
  float height = procedural::kCylinderHeight,
  float radius = procedural::kCylinderRadius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a cone.
OXGN_DATA_NDAPI auto MakeConeMeshAsset(
  unsigned int segments = procedural::kConeSegments,
  float height = procedural::kConeHeight,
  float radius = procedural::kConeRadius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a torus.
OXGN_DATA_NDAPI auto MakeTorusMeshAsset(
  unsigned int major_segments = procedural::kTorusMajorSegments,
  unsigned int minor_segments = procedural::kTorusMinorSegments,
  float major_radius = procedural::kTorusMajorRadius,
  float minor_radius = procedural::kTorusMinorRadius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a new Mesh representing a quad (two triangles in the XY plane).
OXGN_DATA_NDAPI auto MakeQuadMeshAsset(
  float width = procedural::kQuadWidth, float height = procedural::kQuadHeight)
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
