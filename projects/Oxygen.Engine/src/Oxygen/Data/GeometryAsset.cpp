//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>

using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;

namespace {

auto GetNameFromDesc(const char name[oxygen::data::pak::kMaxNameSize])
  -> std::string
{
  std::size_t len = 0;
  while (len < oxygen::data::pak::kMaxNameSize && name[len] != '\0') {
    ++len;
  }
  return { name, len };
}

} // namespace

MeshView::MeshView(const Mesh& mesh, pak::MeshViewDesc desc) noexcept
  : mesh_(mesh)
  , desc_(std::move(desc))
{
  // Enforce design constraints
  CHECK_F(desc_.vertex_count > 0, "MeshView must have at least one vertex");
  CHECK_F(desc_.index_count > 0, "MeshView must have at least one index");
  CHECK_F(desc_.first_vertex + desc_.vertex_count <= mesh.Vertices().size(),
    "MeshView vertex range exceeds mesh vertex count");
  CHECK_F(desc_.first_index + desc_.index_count <= mesh.Indices().size(),
    "MeshView index range exceeds mesh index count");
}

auto MeshView::Vertices() const noexcept -> std::span<const Vertex>
{
  return mesh_.get().Vertices().subspan(desc_.first_vertex, desc_.vertex_count);
}

auto MeshView::Indices() const noexcept -> std::span<const std::uint32_t>
{
  return mesh_.get().Indices().subspan(desc_.first_index, desc_.index_count);
}

SubMesh::SubMesh(
  const Mesh& mesh, pak::SubMeshDesc desc, std::vector<MeshView> meshviews)
  : mesh_(mesh)
  , name_(GetNameFromDesc(desc.name))
  , mesh_views_(std::move(meshviews))
  , bbox_min_(desc.bounding_box_min[0], desc.bounding_box_min[1],
      desc.bounding_box_min[2])
  , bbox_max_(desc.bounding_box_max[0], desc.bounding_box_max[1],
      desc.bounding_box_max[2])
  , desc_(std::move(desc))
{
  // Enforce design constraints
  CHECK_F(!mesh_views_.empty(),
    "SubMesh must have at least one MeshView (1:N constraint)");

  // TODO: resolve the material reference from the descriptor

  // TODO: compute bounding sphere from the vertices of the meshviews
  // ComputeBoundingSphere();
}

SubMesh::SubMesh(const Mesh& mesh, std::string name,
  std::vector<MeshView> meshviews,
  std::shared_ptr<const MaterialAsset> material)
  : mesh_(mesh)
  , name_(std::move(name))
  , mesh_views_(std::move(meshviews))
  , material_(std::move(material))
{
  // Enforce design constraints
  CHECK_F(!mesh_views_.empty(),
    "SubMesh must have at least one MeshView (1:N constraint)");
  CHECK_NOTNULL_F(
    material_, "SubMesh must have exactly one Material (1:1 constraint)");

  // TODO: compute bounding box and sphere from the vertices of the meshviews
  // ComputeBoundingBox();
  // ComputeBoundingSphere();
}

//! Constructs a Mesh with the given name, vertices, and indices.
/*!
  Initializes the mesh asset with the provided name, vertex data, and index
  data. All data is moved in; the asset is immutable after construction.
  @param name Name of the mesh asset
  @param vertices Vertex data (moved)
  @param indices Index data (moved)
  @note Throws (CHECK_F death) if vertices or indices are empty.
*/
Mesh::Mesh(uint32_t lod, pak::MeshDesc desc, std::vector<SubMesh> submeshes)
  : name_(fmt::format("LOD_{}", lod))
  , submeshes_(std::move(submeshes))
  , desc_(std::move(desc))
{
  CHECK_F(!submeshes_.empty(), "Mesh must have at least one submesh");
  ComputeBoundingBox();
  ComputeBoundingSphere();
}

Mesh::Mesh(uint32_t lod, std::vector<Vertex> vertices,
  std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes)
  : name_(fmt::format("LOD_{}", lod))
  , vertices_(std::move(vertices))
  , indices_(std::move(indices))
  , submeshes_(std::move(submeshes))
{
  CHECK_F(!vertices_.empty(), "Mesh must have at least one vertex");
  ComputeBoundingBox();
  ComputeBoundingSphere();
}

Mesh::Mesh(uint32_t lod, std::vector<Vertex> vertices,
  std::vector<std::uint32_t> indices)
  : name_(fmt::format("LOD_{}", lod))
  , vertices_(std::move(vertices))
  , indices_(std::move(indices))
{
  CHECK_F(!vertices_.empty(), "Mesh must have at least one vertex");
  ComputeBoundingBox();
  ComputeBoundingSphere();
}

//! Computes the axis-aligned bounding box (AABB) from the mesh's vertex data.
/*!
  Scans all vertex positions and updates `bbox_min_` and `bbox_max_` to enclose
  all vertices. If the mesh is empty, both min and max are set to zero.
*/
auto Mesh::ComputeBoundingBox() -> void
{
  if (vertices_.empty()) {
    bbox_min_ = bbox_max_ = glm::vec3(0.0f);
    return;
  }
  bbox_min_ = bbox_max_ = vertices_.front().position;
  for (const auto& v : vertices_) {
    bbox_min_ = glm::min(bbox_min_, v.position);
    bbox_max_ = glm::max(bbox_max_, v.position);
  }
}

//! Computes the bounding sphere for the mesh in local space.
/*!
  Computes a bounding sphere that encloses the mesh in local space by
  fitting a sphere to the AABB corners.

  @note This is used for culling and render list construction.
*/
auto Mesh::ComputeBoundingSphere() -> void
{
  // Use Ritter's algorithm or fit sphere to AABB corners (simple, conservative)
  // Here: fit sphere to AABB corners
  const glm::vec3& min = bbox_min_;
  const glm::vec3& max = bbox_max_;
  glm::vec3 corners[8] = {
    { min.x, min.y, min.z },
    { max.x, min.y, min.z },
    { min.x, max.y, min.z },
    { max.x, max.y, min.z },
    { min.x, min.y, max.z },
    { max.x, min.y, max.z },
    { min.x, max.y, max.z },
    { max.x, max.y, max.z },
  };
  glm::vec3 center(0.0f);
  for (int i = 0; i < 8; ++i) {
    center += corners[i];
  }
  center /= 8.0f;
  float radius = 0.0f;
  for (int i = 0; i < 8; ++i) {
    radius = std::max(radius, glm::distance(center, corners[i]));
  }
  bounding_sphere_ = glm::vec4(center, radius);
}

//! Adds a submesh containing its meshviews and a material.
auto Mesh::AddSubMesh(std::string name, std::vector<MeshView> meshviews,
  std::shared_ptr<const MaterialAsset> material) -> void
{
  submeshes_.emplace_back(
    *this, std::move(name), std::move(meshviews), std::move(material));
}

using oxygen::data::MeshBuilder;

//! Builds and returns the immutable Mesh.
auto MeshBuilder::Build() -> std::shared_ptr<Mesh>
{
  CHECK_F(!vertices_.empty(), "Mesh must have vertices");
  CHECK_F(!submeshes_.empty(), "Mesh must have at least one submesh");

  // Create the Mesh object
  auto mesh = std::shared_ptr<Mesh>(new Mesh(lod_, vertices_, indices_));
  mesh->SetName(name_);

  // For each submesh spec, create MeshViews and SubMesh, then add to mesh
  for (const auto& spec : submeshes_) {
    SubMesh submesh(*mesh, spec.name, spec.material);
    for (const auto& view_desc : spec.mesh_views) {
      submesh.AddMeshViewInternal(std::move(view_desc));
    }
    mesh->AddSubMeshInternal(std::move(submesh));
  }

  return mesh;
}
