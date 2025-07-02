//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

// Forward declaration
class MaterialAsset;

//! Immutable, non-owning view of a mesh's geometry data.
/*!
 MeshView is a lightweight, value-type view into a contiguous subrange of a
 mesh's vertex and index data. It does not own any memory and simply references
 a range by offset and count, similar to std::span or std::string_view. Only
 Mesh can construct MeshView instances, ensuring safe, non-owning access to
 mesh data for rendering, culling, and asset management.

 @warning MeshView is only valid as long as the owning Mesh is alive. Do
 not retain MeshView instances beyond the lifetime of the Mesh that created
 them.

 ### Key Features

 - **Non-owning**: Does not manage memory; references mesh data owned by
   Mesh.
 - **Lightweight**: Value type, cheap to copy and pass by value.
 - **Encapsulated**: Only Mesh can create MeshView instances.

 ### Usage Patterns

 ```cpp
 auto view = mesh_asset.MakeView(0, 100, 0, 300);
 for (const auto& v : view.Vertices()) { ... }
 ```

 @note MeshView is invalid if the underlying mesh data is destroyed.
 @see Mesh, Vertex
*/
class MeshView {
public:
  // Only Mesh should construct MeshView. Do not construct directly.
  MeshView(std::span<const Vertex> vertices,
    std::span<const std::uint32_t> indices) noexcept
    : vertices_(vertices)
    , indices_(indices)
  {
  }

  ~MeshView() = default;

  OXYGEN_DEFAULT_COPYABLE(MeshView)
  OXYGEN_DEFAULT_MOVABLE(MeshView)

  [[nodiscard]] constexpr auto Vertices() const noexcept
    -> std::span<const Vertex>
  {
    return vertices_;
  }
  [[nodiscard]] constexpr auto Indices() const noexcept
    -> std::span<const std::uint32_t>
  {
    return indices_;
  }

private:
  std::span<const Vertex> vertices_ {};
  std::span<const std::uint32_t> indices_ {};
};

//! Represents a submesh within a mesh asset.
/*!
 SubMesh groups one or more contiguous MeshViews and associates them with a
 material. SubMeshes are logical partitions of a mesh for rendering, material
 binding, and culling. Only Mesh can construct SubMesh instances, ensuring
 correct ownership and encapsulation.

 ### Design Constraints

 - **1:N MeshViews**: Each SubMesh must contain at least one MeshView.
 - **1:1 Material**: Each SubMesh must reference exactly one MaterialAsset.
 - Construction will fail (assert) if these constraints are violated.

 ### Key Features

 - **Material Association**: Each submesh references a MaterialAsset.
 - **Multiple Views**: Supports multiple MeshViews for complex submesh layouts.
 - **Encapsulated**: Only Mesh can create SubMesh instances.

 ### Usage Patterns

 ...

 @see Mesh, MeshView, MaterialAsset
*/
class SubMesh {
public:
  // Only Mesh should construct SubMesh. Do not construct directly.
  SubMesh(std::string name, std::vector<MeshView> meshviews,
    std::shared_ptr<const MaterialAsset> material)
    : name_(std::move(name))
    , mesh_views_(std::move(meshviews))
    , material_(std::move(material))
  {
    // Enforce design constraints
    assert(!mesh_views_.empty()
      && "SubMesh must have at least one MeshView (1:N constraint)");
    assert(material_ != nullptr
      && "SubMesh must have exactly one Material (1:1 constraint)");
  }
  ~SubMesh() = default;

  OXYGEN_MAKE_NON_COPYABLE(SubMesh)
  OXYGEN_DEFAULT_MOVABLE(SubMesh)

  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }
  [[nodiscard]] auto MeshViews() const noexcept -> std::span<const MeshView>
  {
    return mesh_views_;
  }
  [[nodiscard]] auto Material() const noexcept
    -> std::shared_ptr<const MaterialAsset>
  {
    return material_;
  }

private:
  std::string name_;
  std::vector<MeshView> mesh_views_;
  std::shared_ptr<const MaterialAsset> material_;
};

//! Immutable, shareable mesh asset containing geometry data and submeshes.
/*!
 Mesh owns and manages the lifetime of mesh geometry data (vertices and
 indices) and provides immutable, non-owning MeshView instances for use by
 scene, rendering, and asset management systems. Mesh enforces memory
 safety, encapsulation, and efficient sharing of mesh data. All submeshes are
 constructed and owned by Mesh, and each submesh references a material.

 ### Key Features

 - **Immutable**: No mutators; all data is set at construction.
 - **Shareable**: Designed for safe sharing across systems.
 - **View Creation**: Only Mesh can create MeshView and SubMesh instances.
 - **Thread Safety**: Mesh is **not** thread-safe for concurrent creation
   or submesh addition. After construction, Mesh is fully immutable and
   safe for concurrent read access.
 - **Validity**: A Mesh is only valid if it contains at least one submesh.

 ### Usage Patterns

 ```cpp
 std::vector<Vertex> vertices = ...;
 std::vector<std::uint32_t> indices = ...;
 auto mesh = std::make_shared<Mesh>("MyMesh",
     std::move(vertices), std::move(indices));

 // The Mesh is only valid if it has at least one submesh

 std::shared_ptr<MaterialAsset> material = ...;
 std::vector<MeshView> views;
 views.push_back(mesh->MakeView(0, mesh->VertexCount(), 0, mesh->IndexCount()));
 mesh->AddSubMesh("default", std::move(views), material);
 ```

 @note Mesh is invalid if it contains no submeshes.
 @see MeshView, SubMesh, Vertex, MaterialAsset
*/
class Mesh final {
public:
  //! Constructs a Mesh with the given name, vertices, and indices.
  OXGN_DATA_API Mesh(std::string name, std::vector<Vertex> vertices,
    std::vector<std::uint32_t> indices);

  ~Mesh() = default;

  OXYGEN_MAKE_NON_COPYABLE(Mesh)
  OXYGEN_DEFAULT_MOVABLE(Mesh)

  //! Returns the name of the mesh asset.
  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }

  //! Returns a span of all vertices.
  [[nodiscard]] auto Vertices() const noexcept -> std::span<const Vertex>
  {
    return vertices_;
  }

  //! Returns a span of all indices.
  [[nodiscard]] auto Indices() const noexcept -> std::span<const std::uint32_t>
  {
    return indices_;
  }

  //! Returns the number of vertices.
  [[nodiscard]] auto VertexCount() const noexcept -> std::size_t
  {
    return vertices_.size();
  }

  //! Returns the number of indices.
  [[nodiscard]] auto IndexCount() const noexcept -> std::size_t
  {
    return indices_.size();
  }

  //! Returns true if the mesh uses an index buffer (i.e., has indices).
  [[nodiscard]] auto IsIndexed() const noexcept -> bool
  {
    return !indices_.empty();
  }

  //! Creates a MeshView for a subrange of the mesh's vertex and index buffers.
  OXGN_DATA_NDAPI auto MakeView(std::size_t vertex_offset,
    std::size_t vertex_count, std::size_t index_offset,
    std::size_t index_count) const -> MeshView;

  //! Adds a submesh containing its meshviews and a material.
  OXGN_DATA_API auto AddSubMesh(std::string name,
    std::vector<MeshView> meshviews,
    std::shared_ptr<const MaterialAsset> material) -> void;

  //! Returns a span of all submeshes.
  [[nodiscard]] auto SubMeshes() const noexcept -> std::span<const SubMesh>
  {
    return submeshes_;
  }

  //! Returns the minimum corner of the mesh's axis-aligned bounding box (AABB).
  [[nodiscard]] auto BoundingBoxMin() const noexcept -> const glm::vec3&
  {
    return bbox_min_;
  }

  //! Returns the maximum corner of the mesh's axis-aligned bounding box (AABB).
  [[nodiscard]] auto BoundingBoxMax() const noexcept -> const glm::vec3&
  {
    return bbox_max_;
  }

  //! Returns the local-space bounding sphere (center.xyz, radius.w)
  [[nodiscard]] auto BoundingSphere() const noexcept -> const glm::vec4&
  {
    return bounding_sphere_;
  }

  //! Returns true if the mesh asset contains at least one submesh.
  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return !submeshes_.empty();
  }

private:
  //! Computes the axis-aligned bounding box (AABB) from the mesh's vertex data.
  /*!
   Scans all vertex positions and updates bbox_min_ and bbox_max_ to enclose all
   vertices. Should be called during construction or after loading new vertex
   data.
   @note This is a private utility for internal use only.
  */
  auto ComputeBoundingBox() -> void;
  auto ComputeBoundingSphere() -> void;

  std::string name_;
  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<SubMesh> submeshes_;
  glm::vec3 bbox_min_ {};
  glm::vec3 bbox_max_ {};
  glm::vec4 bounding_sphere_ { 0.0f, 0.0f, 0.0f, 0.0f };
};

} // namespace oxygen::data
