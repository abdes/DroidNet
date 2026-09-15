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

//! Creates vertex/index buffers for a unit axis-aligned cube centred at
//! the origin, with separate vertices and hard normals for each face.
OXGN_DATA_NDAPI auto MakeCubeMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates a unit cube surface with welded vertices and configurable
//! subdivisions per face edge.
OXGN_DATA_NDAPI auto MakeSubdividedCubeMeshAsset(
  unsigned int segments = procedural::kSubdividedCubeSegments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a radius-0.5 UV sphere centred at the
//! origin, with poles along Z.
OXGN_DATA_NDAPI auto MakeSphereMeshAsset(
  unsigned int latitude_segments = procedural::kSphereLatitudeSegments,
  unsigned int longitude_segments = procedural::kSphereLongitudeSegments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a radius-0.5 icosphere centred at the
//! origin.
OXGN_DATA_NDAPI auto MakeIcoSphereMeshAsset(
  unsigned int subdivision_level = procedural::kIcoSphereSubdivisionLevel)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Alias of MakeIcoSphereMeshAsset for authoring/runtime readability.
OXGN_DATA_NDAPI auto MakeGeodesicSphereMeshAsset(
  unsigned int subdivision_level = procedural::kIcoSphereSubdivisionLevel)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a square XY grid at z = 0, centred at
//! the origin. The second segment count subdivides Y.
OXGN_DATA_NDAPI auto MakePlaneMeshAsset(
  unsigned int x_segments = procedural::kPlaneXSegments,
  unsigned int z_segments = procedural::kPlaneZSegments,
  float size = procedural::kPlaneSize)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a Z-axis cylinder with caps at
//! z = -height/2 and z = +height/2.
OXGN_DATA_NDAPI auto MakeCylinderMeshAsset(
  unsigned int segments = procedural::kCylinderSegments,
  float height = procedural::kCylinderHeight,
  float radius = procedural::kCylinderRadius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a Z-axis cone with its base at
//! z = -height/2 and its apex at z = +height/2.
OXGN_DATA_NDAPI auto MakeConeMeshAsset(
  unsigned int segments = procedural::kConeSegments,
  float height = procedural::kConeHeight,
  float radius = procedural::kConeRadius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a torus centred at the origin, with its
//! main ring in XY around the Z axis.
OXGN_DATA_NDAPI auto MakeTorusMeshAsset(
  unsigned int major_segments = procedural::kTorusMajorSegments,
  unsigned int minor_segments = procedural::kTorusMinorSegments,
  float major_radius = procedural::kTorusMajorRadius,
  float minor_radius = procedural::kTorusMinorRadius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for a rectangle in XY at z = 0, centred at
//! the origin, with width along X, height along Y and normals along +Z.
OXGN_DATA_NDAPI auto MakeQuadMeshAsset(
  float width = procedural::kQuadWidth, float height = procedural::kQuadHeight)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

//! Creates vertex/index buffers for an editor/debug arrow pointing along +Z.
//! Its z extent is [-0.1, 0.78], so its bounding box is not centred at zero.
OXGN_DATA_NDAPI auto MakeArrowGizmoMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

OXGN_DATA_NDAPI auto GenerateMeshBuffers(
  std::string_view name, std::span<const std::byte> param_blob)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

OXGN_DATA_NDAPI auto GenerateMesh(std::string_view name,
  std::span<const std::byte> param_blob) -> std::unique_ptr<Mesh>;

} // namespace oxygen::data
