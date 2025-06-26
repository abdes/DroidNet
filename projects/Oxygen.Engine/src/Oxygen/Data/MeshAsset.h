//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/MeshView.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Immutable, shareable mesh asset containing geometry data and views.
/*!
 MeshAsset owns and manages the lifetime of mesh geometry data (vertices and
 indices) and provides immutable, non-owning MeshView instances for use by
 scene, rendering, and asset management systems. MeshAsset enforces memory
 safety, encapsulation, and efficient sharing of mesh data.

 ### Key Features

 - **Immutable**: No mutators; all data is set at construction.
 - **Shareable**: Designed for safe sharing across systems.
 - **View Creation**: Only MeshAsset can create MeshView instances.
 - **Thread Safety**: MeshAsset is **not** thread-safe for concurrent creation
 or view addition. All construction and view creation must occur on the same
 thread, or be externally synchronized. After construction, MeshAsset is fully
 immutable and safe for concurrent read access.

 ### Usage Patterns

 ```cpp
 auto asset = std::make_shared<MeshAsset>(name, vertices, indices);
 asset->CreateView("LOD0", 0, 100, 0, 300);
 // Use asset->Views() in scene, renderer, etc.
 ```

 @see MeshView, Vertex
*/
class MeshAsset final {
public:
  //! Constructs a MeshAsset with the given name, vertices, and indices.
  OXGN_DATA_API MeshAsset(std::string name, std::vector<Vertex> vertices,
    std::vector<std::uint32_t> indices);

  ~MeshAsset() = default;

  OXYGEN_MAKE_NON_COPYABLE(MeshAsset)
  OXYGEN_DEFAULT_MOVABLE(MeshAsset)

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

  //! Creates and stores a MeshView for a subrange of the mesh data.
  OXGN_DATA_API auto CreateView(std::string_view name,
    std::size_t vertex_offset, std::size_t vertex_count,
    std::size_t index_offset, std::size_t index_count) noexcept -> void;

  //! Returns a span of all mesh views (submeshes).
  /*!
   Provides read-only access to all submesh views (MeshView) in this mesh. Use
   standard C++20 ranges and algorithms for traversal and lookup.

   Example usage:
   ```cpp
   // Traverse all submeshes
   for (const auto& view : mesh_asset.Views()) {
       // ...
   }
   // Lookup by name
   auto views = mesh_asset.Views();
   auto it = std::ranges::find_if(views, [](const MeshView& v) { return v.Name()
   == "leg"; }); if (it != views.end()) {
       // use *it
   }
   ```
   @return A span of all MeshView submeshes in this mesh asset.
   @note The returned span is always valid and reflects the immutable set of
   submeshes.
  */
  [[nodiscard]] auto Views() const noexcept -> std::span<const MeshView>
  {
    return views_;
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

private:
  //! Computes the axis-aligned bounding box (AABB) from the mesh's vertex data.
  /*!
   Scans all vertex positions and updates bbox_min_ and bbox_max_ to enclose all
   vertices. Should be called during construction or after loading new vertex
   data.
   @note This is a private utility for internal use only.
  */
  auto ComputeBoundingBox() -> void;

  std::string name_;
  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<MeshView> views_;
  glm::vec3 bbox_min_ {};
  glm::vec3 bbox_max_ {};
};

} // namespace oxygen::data
