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

namespace oxygen::vortex::sceneprep::testing {

using oxygen::data::AssetKey;
using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshBuilder;
using oxygen::data::Vertex;
namespace pak = oxygen::data::pak;

[[nodiscard]] inline auto MakeStandardMeshDesc(const glm::vec3 bounds_min,
  const glm::vec3 bounds_max) -> oxygen::data::pak::geometry::MeshDesc
{
  using oxygen::data::MeshType;
  oxygen::data::pak::geometry::MeshDesc desc {};
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
  -> oxygen::data::pak::geometry::SubMeshDesc
{
  oxygen::data::pak::geometry::SubMeshDesc desc {
    .name = {},
    .material_asset_key = {},
    .mesh_view_count = mesh_view_count,
    .bounding_box_min = { bounds_min.x, bounds_min.y, bounds_min.z, },
    .bounding_box_max = { bounds_max.x, bounds_max.y, bounds_max.z, },
  };
  return desc;
}

//! Create a simple triangle mesh for tests.
inline auto MakeSimpleMesh(const uint32_t lod, const std::string_view name = {})
  -> std::shared_ptr<oxygen::data::Mesh>
{
  std::vector<Vertex> vertices(3);
  vertices.at(0).position = {
    -1.0F,
    0.0F,
    0.0F,
  };
  vertices.at(1).position = {
    1.0F,
    0.0F,
    0.0F,
  };
  vertices.at(2).position = {
    0.0F,
    1.0F,
    0.0F,
  };
  std::vector<uint32_t> idx = {
    0,
    1,
    2,
  };
  const auto mat = MaterialAsset::CreateDefault();
  auto builder = MeshBuilder(lod, name);
  const auto mesh_desc = MakeStandardMeshDesc(
    glm::vec3(-1.0F, 0.0F, 0.0F), glm::vec3(1.0F, 1.0F, 0.0F));
  const auto submesh_desc = MakeSubMeshDesc(
    glm::vec3(-1.0F, 0.0F, 0.0F), glm::vec3(1.0F, 1.0F, 0.0F));
  builder.WithVertices(vertices).WithIndices(idx).WithDescriptor(mesh_desc);
  builder.BeginSubMesh("S0", mat)
    .WithDescriptor(submesh_desc)
    .WithMeshView({
      .first_index = 0U,
      .index_count
      = static_cast<pak::geometry::MeshViewDesc::BufferIndexT>(idx.size()),
      .first_vertex = 0U,
      .vertex_count
      = static_cast<pak::geometry::MeshViewDesc::BufferIndexT>(vertices.size()),
    })
    .EndSubMesh();
  return {
    builder.Build(),
  };
}

struct MeshLayout {
  uint32_t lod;
  std::size_t submesh_count;
};

//! Create a mesh with the requested LOD index and submesh count.
inline auto MakeMeshWithSubmeshes(const MeshLayout layout)
  -> std::shared_ptr<oxygen::data::Mesh>
{
  std::vector<Vertex> vertices(4);
  vertices.at(0).position = {
    -1,
    -1,
    0,
  };
  vertices.at(1).position = {
    1,
    -1,
    0,
  };
  vertices.at(2).position = {
    1,
    1,
    0,
  };
  vertices.at(3).position = {
    -1,
    1,
    0,
  };
  std::vector<uint32_t> idx = {
    0,
    1,
    2,
    2,
    3,
    0,
  };
  const auto mat = MaterialAsset::CreateDefault();
  MeshBuilder b(layout.lod);
  const auto mesh_desc = MakeStandardMeshDesc(
    glm::vec3(-1.0F, -1.0F, 0.0F), glm::vec3(1.0F, 1.0F, 0.0F));
  const auto submesh_desc = MakeSubMeshDesc(
    glm::vec3(-1.0F, -1.0F, 0.0F), glm::vec3(1.0F, 1.0F, 0.0F));
  b.WithVertices(vertices).WithIndices(idx).WithDescriptor(mesh_desc);
  for (std::size_t s = 0; s < layout.submesh_count; ++s) {
    b.BeginSubMesh("SM", mat)
      .WithDescriptor(submesh_desc)
      .WithMeshView({
        .first_index = 0U,
        .index_count
        = static_cast<pak::geometry::MeshViewDesc::BufferIndexT>(idx.size()),
        .first_vertex = 0U,
        .vertex_count = static_cast<pak::geometry::MeshViewDesc::BufferIndexT>(
          vertices.size()),
      })
      .EndSubMesh();
  }
  return {
    b.Build(),
  };
}

//! Create a mesh with submeshes placed at provided centers (spread test mesh).
inline auto MakeSpreadMesh(uint32_t lod, const std::vector<glm::vec3>& centers,
  const glm::vec3 mesh_bounds_min, const glm::vec3 mesh_bounds_max,
  const std::vector<std::pair<glm::vec3, glm::vec3>>& submesh_bounds)
  -> std::shared_ptr<oxygen::data::Mesh>
{
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
    const glm::vec3 c = centers.at(s);
    oxygen::data::Vertex v0 {};
    oxygen::data::Vertex v1 {};
    oxygen::data::Vertex v2 {};
    oxygen::data::Vertex v3 {};
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
      .WithDescriptor(MakeSubMeshDesc(
        submesh_bounds.at(s).first, submesh_bounds.at(s).second))
      .WithMeshView({
        .first_index = base_i,
        .index_count
        = static_cast<pak::geometry::MeshViewDesc::BufferIndexT>(6),
        .first_vertex = base_v,
        .vertex_count
        = static_cast<pak::geometry::MeshViewDesc::BufferIndexT>(4),
      })
      .EndSubMesh();
  }

  return {
    b.Build(),
  };
}

//! Build a GeometryAsset with the given LOD count and bounding box.
inline auto MakeGeometryWithLods(const size_t lod_count, const glm::vec3 bb_min,
  const glm::vec3 bb_max) -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  data::pak::geometry::GeometryAssetDesc desc {};
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
  data::pak::geometry::GeometryAssetDesc desc {};
  desc.lod_count = static_cast<uint32_t>(per_lod_counts.size());
  desc.bounding_box_min[0] = -1.0F;
  desc.bounding_box_min[1] = -1.0F;
  desc.bounding_box_min[2] = -1.0F;
  desc.bounding_box_max[0] = 1.0F;
  desc.bounding_box_max[1] = 1.0F;
  desc.bounding_box_max[2] = 1.0F;

  std::vector<std::shared_ptr<data::Mesh>> lods;
  lods.reserve(per_lod_counts.size());
  uint32_t lod = 0;
  for (const auto count : per_lod_counts) {
    lods.emplace_back(MakeMeshWithSubmeshes({
      .lod = lod++,
      .submesh_count = count,
    }));
  }
  return std::make_shared<data::GeometryAsset>(
    AssetKey {}, desc, std::move(lods));
}

} // namespace oxygen::vortex::sceneprep::testing
