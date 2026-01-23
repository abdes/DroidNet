//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>

namespace oxygen::engine::sceneprep::testing {

[[nodiscard]] inline auto MakeStandardMeshDesc(const glm::vec3 bounds_min,
  const glm::vec3 bounds_max) -> oxygen::data::pak::MeshDesc
{
  using oxygen::data::MeshType;
  oxygen::data::pak::MeshDesc desc {};
  desc.mesh_type
    = static_cast<std::underlying_type_t<MeshType>>(MeshType::kStandard);
  desc.info.standard.bounding_box_min[0] = bounds_min.x;
  desc.info.standard.bounding_box_min[1] = bounds_min.y;
  desc.info.standard.bounding_box_min[2] = bounds_min.z;
  desc.info.standard.bounding_box_max[0] = bounds_max.x;
  desc.info.standard.bounding_box_max[1] = bounds_max.y;
  desc.info.standard.bounding_box_max[2] = bounds_max.z;
  return desc;
}

[[nodiscard]] inline auto MakeSubMeshDesc(const glm::vec3 bounds_min,
  const glm::vec3 bounds_max, const uint32_t mesh_view_count = 1U)
  -> oxygen::data::pak::SubMeshDesc
{
  oxygen::data::pak::SubMeshDesc desc {
    .name = {},
    .material_asset_key = {},
    .mesh_view_count = mesh_view_count,
    .bounding_box_min = { bounds_min.x, bounds_min.y, bounds_min.z },
    .bounding_box_max = { bounds_max.x, bounds_max.y, bounds_max.z },
  };
  return desc;
}

//! Create a simple triangle mesh for tests.
inline auto MakeSimpleMesh(const uint32_t lod, const std::string_view name = {})
  -> std::shared_ptr<oxygen::data::Mesh>
{
  using namespace oxygen::data;
  std::vector<Vertex> vertices(3);
  vertices[0].position = { -1.0f, 0.0f, 0.0f };
  vertices[1].position = { 1.0f, 0.0f, 0.0f };
  vertices[2].position = { 0.0f, 1.0f, 0.0f };
  std::vector<uint32_t> idx = { 0, 1, 2 };
  const auto mat = MaterialAsset::CreateDefault();
  auto builder = MeshBuilder(lod, name);
  const auto mesh_desc = MakeStandardMeshDesc(
    glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
  const auto submesh_desc = MakeSubMeshDesc(
    glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
  builder.WithVertices(vertices).WithIndices(idx).WithDescriptor(mesh_desc);
  builder.BeginSubMesh("S0", mat)
    .WithDescriptor(submesh_desc)
    .WithMeshView({ .first_index = 0u,
      .index_count = static_cast<pak::MeshViewDesc::BufferIndexT>(idx.size()),
      .first_vertex = 0u,
      .vertex_count
      = static_cast<pak::MeshViewDesc::BufferIndexT>(vertices.size()) })
    .EndSubMesh();
  return std::shared_ptr<Mesh>(builder.Build().release());
}

//! Create a mesh with a specified number of submeshes.
inline auto MakeMeshWithSubmeshes(const uint32_t lod,
  const std::size_t submesh_count) -> std::shared_ptr<oxygen::data::Mesh>
{
  using namespace oxygen::data;
  std::vector<Vertex> vertices(4);
  vertices[0].position = { -1, -1, 0 };
  vertices[1].position = { 1, -1, 0 };
  vertices[2].position = { 1, 1, 0 };
  vertices[3].position = { -1, 1, 0 };
  std::vector<uint32_t> idx = { 0, 1, 2, 2, 3, 0 };
  const auto mat = MaterialAsset::CreateDefault();
  MeshBuilder b(lod);
  const auto mesh_desc = MakeStandardMeshDesc(
    glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
  const auto submesh_desc = MakeSubMeshDesc(
    glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
  b.WithVertices(vertices).WithIndices(idx).WithDescriptor(mesh_desc);
  for (std::size_t s = 0; s < submesh_count; ++s) {
    b.BeginSubMesh("SM", mat)
      .WithDescriptor(submesh_desc)
      .WithMeshView({ .first_index = 0u,
        .index_count = static_cast<pak::MeshViewDesc::BufferIndexT>(idx.size()),
        .first_vertex = 0u,
        .vertex_count
        = static_cast<pak::MeshViewDesc::BufferIndexT>(vertices.size()) })
      .EndSubMesh();
  }
  return std::shared_ptr<Mesh>(b.Build().release());
}

//! Create a mesh with submeshes placed at provided centers (spread test mesh).
inline auto MakeSpreadMesh(uint32_t lod, const std::vector<glm::vec3>& centers,
  const glm::vec3 mesh_bounds_min, const glm::vec3 mesh_bounds_max,
  const std::vector<std::pair<glm::vec3, glm::vec3>>& submesh_bounds)
  -> std::shared_ptr<oxygen::data::Mesh>
{
  using namespace oxygen::data;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> idx;
  vertices.reserve(centers.size() * 4);
  idx.reserve(centers.size() * 6);
  auto mat = MaterialAsset::CreateDefault();
  MeshBuilder b(lod);
  b.WithDescriptor(MakeStandardMeshDesc(mesh_bounds_min, mesh_bounds_max));

  CHECK_F(submesh_bounds.size() == centers.size(),
    "Submesh bounds count must match centers count");

  for (size_t s = 0; s < centers.size(); ++s) {
    const auto base_v = static_cast<uint32_t>(vertices.size());
    const glm::vec3 c = centers[s];
    oxygen::data::Vertex v0 {}, v1 {}, v2 {}, v3 {};
    v0.position = c + glm::vec3(-1, -1, 0);
    v1.position = c + glm::vec3(1, -1, 0);
    v2.position = c + glm::vec3(1, 1, 0);
    v3.position = c + glm::vec3(-1, 1, 0);
    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);
    const auto base_i = static_cast<uint32_t>(idx.size());
    idx.push_back(base_v + 0);
    idx.push_back(base_v + 1);
    idx.push_back(base_v + 2);
    idx.push_back(base_v + 2);
    idx.push_back(base_v + 3);
    idx.push_back(base_v + 0);

    b.WithVertices(vertices).WithIndices(idx);
    b.BeginSubMesh("SMs", mat)
      .WithDescriptor(
        MakeSubMeshDesc(submesh_bounds[s].first, submesh_bounds[s].second))
      .WithMeshView({ .first_index = base_i,
        .index_count = static_cast<pak::MeshViewDesc::BufferIndexT>(6),
        .first_vertex = base_v,
        .vertex_count = static_cast<pak::MeshViewDesc::BufferIndexT>(4) })
      .EndSubMesh();
  }

  return std::shared_ptr<Mesh>(b.Build().release());
}

//! Build a GeometryAsset with the given LOD count and bounding box.
inline auto MakeGeometryWithLods(const size_t lod_count, const glm::vec3 bb_min,
  const glm::vec3 bb_max) -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using namespace oxygen::data;
  data::pak::GeometryAssetDesc desc {};
  desc.lod_count = static_cast<uint32_t>(lod_count);
  desc.bounding_box_min[0] = bb_min.x;
  desc.bounding_box_min[1] = bb_min.y;
  desc.bounding_box_min[2] = bb_min.z;
  desc.bounding_box_max[0] = bb_max.x;
  desc.bounding_box_max[1] = bb_max.y;
  desc.bounding_box_max[2] = bb_max.z;

  std::vector<std::shared_ptr<data::Mesh>> lods;
  lods.reserve(lod_count);
  for (size_t i = 0; i < lod_count; ++i) {
    lods.emplace_back(MakeSimpleMesh(static_cast<uint32_t>(i)));
  }
  return std::make_shared<data::GeometryAsset>(
    AssetKey {}, desc, std::move(lods));
}

//! Build a GeometryAsset with per-LOD submesh counts.
inline auto MakeGeometryWithLODSubmeshes(
  const std::initializer_list<std::size_t> per_lod_counts)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using namespace oxygen::data;
  data::pak::GeometryAssetDesc desc {};
  desc.lod_count = static_cast<uint32_t>(per_lod_counts.size());
  desc.bounding_box_min[0] = -1.0f;
  desc.bounding_box_min[1] = -1.0f;
  desc.bounding_box_min[2] = -1.0f;
  desc.bounding_box_max[0] = 1.0f;
  desc.bounding_box_max[1] = 1.0f;
  desc.bounding_box_max[2] = 1.0f;

  std::vector<std::shared_ptr<data::Mesh>> lods;
  lods.reserve(per_lod_counts.size());
  uint32_t lod = 0;
  for (const auto count : per_lod_counts) {
    lods.emplace_back(MakeMeshWithSubmeshes(lod++, count));
  }
  return std::make_shared<data::GeometryAsset>(
    AssetKey {}, desc, std::move(lods));
}

} // namespace oxygen::engine::sceneprep::testing
