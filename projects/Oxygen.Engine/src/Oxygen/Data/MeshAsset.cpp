//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/MeshAsset.h>

namespace oxygen::data {

//! Constructs a MeshAsset with the given name, vertices, and indices.
/*!
  Initializes the mesh asset with the provided name, vertex data, and index
  data. All data is moved in; the asset is immutable after construction.
  @param name Name of the mesh asset
  @param vertices Vertex data (moved)
  @param indices Index data (moved)
  @note Throws (CHECK_F death) if vertices or indices are empty.
*/
MeshAsset::MeshAsset(std::string name, std::vector<Vertex> vertices,
  std::vector<std::uint32_t> indices)
  : name_(std::move(name))
  , vertices_(std::move(vertices))
  , indices_(std::move(indices))
{
  CHECK_F(!vertices_.empty(), "MeshAsset must have at least one vertex");
  CHECK_F(!indices_.empty(), "MeshAsset must have at least one index");
  ComputeBoundingBox();
  ComputeBoundingSphere();
}

//! Creates and stores a MeshView for a subrange of the mesh data.
/*!
  Adds a MeshView describing a subrange of the mesh's vertex and index data to
  the views_ collection and returns a reference to it. No validation is
  performed; the caller must ensure the ranges are valid.

  @param name Name for the view (for debugging/identification)
  @param vertex_offset Offset into the vertex array
  @param vertex_count Number of vertices in the view
  @param index_offset Offset into the index array
  @param index_count Number of indices in the view
  @return Reference to the stored MeshView (non-owning, immutable)

  @warning No validation is performed; caller must ensure ranges are valid.
*/
auto MeshAsset::CreateView(std::string_view name,
  const std::size_t vertex_offset, const std::size_t vertex_count,
  const std::size_t index_offset, const std::size_t index_count) noexcept
  -> void
{
  CHECK_F(vertex_offset + vertex_count <= vertices_.size(),
    "MeshView vertex range out of bounds");
  CHECK_F(index_offset + index_count <= indices_.size(),
    "MeshView index range out of bounds");
  views_.emplace_back(name,
    std::span<const Vertex>(vertices_.data() + vertex_offset, vertex_count),
    std::span<const std::uint32_t>(
      indices_.data() + index_offset, index_count));
}

//! Computes the axis-aligned bounding box (AABB) from the mesh's vertex data.
/*!
  Scans all vertex positions and updates `bbox_min_` and `bbox_max_` to enclose
  all vertices. If the mesh is empty, both min and max are set to zero.
*/
auto MeshAsset::ComputeBoundingBox() -> void
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
auto MeshAsset::ComputeBoundingSphere() -> void
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

} // namespace oxygen::data
