//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

// Forward declaration
class MaterialAsset;
class Mesh;

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
  OXGN_DATA_API MeshView(const Mesh& mesh, pak::MeshViewDesc desc) noexcept;

  ~MeshView() = default;

  OXYGEN_DEFAULT_COPYABLE(MeshView)
  OXYGEN_DEFAULT_MOVABLE(MeshView)

  OXGN_DATA_NDAPI auto Vertices() const noexcept -> std::span<const Vertex>;

  OXGN_DATA_NDAPI auto Indices() const noexcept
    -> std::span<const std::uint32_t>;

private:
  std::reference_wrapper<const Mesh> mesh_;

  pak::MeshViewDesc desc_ {};
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

 ### Constructor Variants

 - **From PAK data**: Uses pak::SubMeshDesc and resolves material reference
 later
 - **For procedural/builder**: Directly accepts MaterialAsset for
 runtime-generated meshes

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
  ~SubMesh() = default;

  OXYGEN_MAKE_NON_COPYABLE(SubMesh)
  OXYGEN_DEFAULT_MOVABLE(SubMesh)

  //! Returns the submesh name as a string view (for debugging/tools).
  /*!
    Returns the asset name from the descriptor as a string view. The name is
    guaranteed not to exceed pak::kMaxNameSize. This is primarily for debugging
    and tools, not for runtime use.
  */
  auto GetName() const noexcept -> std::string_view { return name_; }

  [[nodiscard]] auto MeshViews() const noexcept -> std::span<const MeshView>
  {
    return mesh_views_;
  }
  [[nodiscard]] auto Material() const noexcept
    -> std::shared_ptr<const MaterialAsset>
  {
    return material_;
  }

protected:
  // Allow MeshBuilder to set up mesh views directly
  friend class MeshBuilder;

  OXGN_DATA_API SubMesh(const Mesh& mesh, std::string name,
    std::shared_ptr<const MaterialAsset> material);

  // Only for MeshBuilder: set mesh views after construction
  void AddMeshViewInternal(pak::MeshViewDesc view_desc)
  {
    mesh_views_.emplace_back(mesh_.get(), std::move(view_desc));
  }

  // Only for SubMeshBuilder: set PAK descriptor for bounding optimization
  void SetDescriptor(pak::SubMeshDesc desc) { desc_ = std::move(desc); }

private:
  //! Computes bounding box and sphere - handles both PAK and procedural cases.
  /*!
   Computes bounding data using the most appropriate method:
   - If PAK descriptor exists: uses pre-computed bounding box
   - If no descriptor: computes bounding box from mesh view vertices
   - Always computes bounding sphere from the resulting bounding box

   Data members are the single source of truth for bounding information.
  */
  auto ComputeBounds() -> void;

  std::reference_wrapper<const Mesh> mesh_;

  std::string name_ {};
  glm::vec3 bbox_min_ {};
  glm::vec3 bbox_max_ {};
  glm::vec4 bounding_sphere_ { 0.0f, 0.0f, 0.0f, 0.0f };
  std::vector<MeshView> mesh_views_;
  std::shared_ptr<const MaterialAsset> material_;

  std::optional<pak::SubMeshDesc> desc_ {};
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
class Mesh {
public:
  ~Mesh() = default;

  OXYGEN_MAKE_NON_COPYABLE(Mesh)
  OXYGEN_DEFAULT_MOVABLE(Mesh)

  //! Returns the mesh name as a string view (for debugging/tools).
  /*!
    Returns the asset name from the descriptor as a string view. The name is
    guaranteed not to exceed pak::kMaxNameSize. This is primarily for debugging
    and tools, not for runtime use.
  */
  auto GetName() const noexcept -> std::string_view { return name_; }

  //! Returns a span of all vertices.
  [[nodiscard]] virtual auto Vertices() const noexcept
    -> std::span<const Vertex>
  {
    return vertices_;
  }

  //! Returns a span of all indices.
  [[nodiscard]] virtual auto Indices() const noexcept
    -> std::span<const std::uint32_t>
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

protected:
  // Allow MeshBuilder to set up submeshes directly
  friend class MeshBuilder;

  // For builder and testing
  OXGN_DATA_API Mesh(uint32_t lod, std::vector<Vertex> vertices,
    std::vector<std::uint32_t> indices);

  // Only for MeshBuilder: add a fully constructed SubMesh
  void AddSubMeshInternal(SubMesh&& submesh)
  {
    submeshes_.emplace_back(std::move(submesh));
  }

  // For builder name override
  void SetName(std::string name) { name_ = std::move(name); }

  // Only for MeshBuilder: set PAK descriptor for bounding optimization
  void SetDescriptor(pak::MeshDesc desc) { desc_ = std::move(desc); }

private:
  //! Computes bounding box and sphere - handles both PAK and procedural cases.
  /*!
   Computes bounding data using the most appropriate method:
   - If PAK descriptor exists: uses pre-computed bounding box
   - If no descriptor: computes bounding box from vertices
   - Always computes bounding sphere from the resulting bounding box

   Data members are the single source of truth for bounding information.
   @note This is a private utility for internal use only.
  */
  auto ComputeBounds() -> void;

  std::string name_ {};
  glm::vec3 bbox_min_ {};
  glm::vec3 bbox_max_ {};
  glm::vec4 bounding_sphere_ { 0.0f, 0.0f, 0.0f, 0.0f };
  std::vector<SubMesh> submeshes_;

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;

  std::optional<pak::MeshDesc> desc_ {};
};

//! Geometry asset as stored in the PAK file resource table.
/*!
  Represents a geometry asset as described by the PAK file's GeometryAssetDesc.
  This is a direct, binary-compatible wrapper for the PAK format, providing
  access to all fields and metadata for rendering and asset management.

  ### Binary Encoding (PAK v1, 256 bytes)

  ```text
  offset size   name                description
  ------ ------ ------------------- -----------------------------------------
  0x00   96     header              AssetHeader (type, name, version, etc.)
  0x60   4      lod_count           Number of LODs (must be >= 1)
  0x64   12     bounding_box_min    AABB min (float[3])
  0x70   12     bounding_box_max    AABB max (float[3])
  0x7C   133    reserved            Reserved/padding to 256 bytes
  0x100 ...     mesh descs          Array MeshDesc[lod_count]
  ```

  @note The mesh LOD array immediately follows the descriptor and is sized
  by `lod_count`.

  @see Mesh, SubMesh, AssetHeader, PakFormat.h
*/
class GeometryAsset : public Asset {
  OXYGEN_TYPED(GeometryAsset)

public:
  GeometryAsset(
    pak::GeometryAssetDesc desc, std::vector<std::shared_ptr<Mesh>> lod_meshes)
    : desc_(std::move(desc))
    , lod_meshes_(std::move(lod_meshes))
  {
  }

  ~GeometryAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GeometryAsset)
  OXYGEN_DEFAULT_MOVABLE(GeometryAsset)

  //! Returns the asset header metadata.
  [[nodiscard]] auto GetHeader() const noexcept -> const pak::AssetHeader&
  {
    return desc_.header;
  }

  //! Returns the minimum corner of the asset's axis-aligned bounding box
  //! (AABB).
  [[nodiscard]] auto BoundingBoxMin() const noexcept -> glm::vec3
  {
    return glm::vec3(desc_.bounding_box_min[0], desc_.bounding_box_min[1],
      desc_.bounding_box_min[2]);
  }

  //! Returns the maximum corner of the asset's axis-aligned bounding box
  //! (AABB).
  [[nodiscard]] auto BoundingBoxMax() const noexcept -> glm::vec3
  {
    return glm::vec3(desc_.bounding_box_max[0], desc_.bounding_box_max[1],
      desc_.bounding_box_max[2]);
  }

  //! Returns a span of all LOD meshes.
  [[nodiscard]] auto Meshes() const noexcept
    -> std::span<const std::shared_ptr<Mesh>>
  {
    return lod_meshes_;
  }

  //! Returns the mesh for the given LOD index, or nullptr if out of range.
  [[nodiscard]] auto MeshAt(size_t lod) const noexcept
    -> const std::shared_ptr<Mesh>&
  {
    static const std::shared_ptr<Mesh> null_mesh;
    if (lod < lod_meshes_.size())
      return lod_meshes_[lod];
    return null_mesh;
  }

  //! Returns the number of LODs (meshes) in the asset.
  [[nodiscard]] auto LodCount() const noexcept -> size_t
  {
    return lod_meshes_.size();
  }

private:
  pak::GeometryAssetDesc desc_ {};
  std::vector<std::shared_ptr<Mesh>> lod_meshes_;
};

//! Builder for a single submesh within a MeshBuilder type-state API.
/*!
  SubMeshBuilder is only constructible by MeshBuilder::BeginSubMesh and is used
  to accumulate mesh views for a single submesh. Only after at least one mesh
  view is added can the submesh be finalized and returned to the parent builder.

  @see MeshBuilder
*/
class SubMeshBuilder {
public:
  SubMeshBuilder(MeshBuilder& parent_builder, std::string name,
    std::shared_ptr<const MaterialAsset> material)
    : parent_(parent_builder)
    , name_(std::move(name))
    , material_(std::move(material))
  {
  }

  ~SubMeshBuilder() = default;

  auto WithMeshView(pak::MeshViewDesc desc) -> SubMeshBuilder&
  {
    mesh_views_.push_back(std::move(desc));
    return *this;
  }

  auto WithDescriptor(pak::SubMeshDesc desc) -> SubMeshBuilder&
  {
    desc_ = std::move(desc);
    return *this;
  }

  auto EndSubMesh() -> MeshBuilder&;

  //! Returns true if at least one mesh view has been added.
  [[nodiscard]] auto HasMeshViews() const noexcept -> bool
  {
    return !mesh_views_.empty();
  }

  //! Accessor for the submesh name.
  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }

  //! Accessor for the submesh material.
  [[nodiscard]] auto Material() const noexcept
    -> const std::shared_ptr<const MaterialAsset>&
  {
    return material_;
  }

  //! Accessor for the mesh view descriptors.
  [[nodiscard]] auto MeshViews() const noexcept
    -> const std::vector<pak::MeshViewDesc>&
  {
    return mesh_views_;
  }

private:
  std::reference_wrapper<MeshBuilder> parent_;
  std::string name_;
  std::shared_ptr<const MaterialAsset> material_;
  std::vector<pak::MeshViewDesc> mesh_views_;
  std::optional<pak::SubMeshDesc> desc_;

  friend class MeshBuilder;
};

//! Builder for constructing immutable Mesh objects with submeshes and views.
/*!
  MeshBuilder provides a fluent, safe API for assembling a Mesh and its
  submeshes/views. It accumulates geometry and submesh definitions, then
  produces a fully immutable Mesh instance.

  @see Mesh, SubMesh, MeshView
*/
//=== MeshBuilder
//===-------------------------------------------------------------//

//! Builder for constructing immutable Mesh objects with submeshes and views.
/*!
  MeshBuilder provides a fluent, type-safe API for assembling a Mesh and its
  submeshes/views. It accumulates geometry and submesh definitions, then
  produces a fully immutable Mesh instance. Submesh construction is enforced
  via the SubMeshBuilder type-state pattern.

  @see Mesh, SubMesh, MeshView, SubMeshBuilder
*/
class MeshBuilder {
public:
  MeshBuilder(uint32_t lod = 0, std::string_view name = {})
    : lod_(lod)
    , name_(name.empty() ? fmt::format("LOD_{}", lod) : std::string(name))
  {
  }

  ~MeshBuilder() = default;

  //! Sets the mesh vertices (replaces any existing vertices).
  auto WithVertices(std::span<const Vertex> vertices) -> MeshBuilder&
  {
    vertices_.assign(vertices.begin(), vertices.end());
    return *this;
  }

  //! Sets the mesh indices (replaces any existing indices).
  auto WithIndices(std::span<const std::uint32_t> indices) -> MeshBuilder&
  {
    indices_.assign(indices.begin(), indices.end());
    return *this;
  }

  auto WithDescriptor(pak::MeshDesc desc) -> MeshBuilder&
  {
    desc_ = std::move(desc);
    return *this;
  }

  //! Begins a new submesh definition. Returns a SubMeshBuilder for mesh view
  //! accumulation.
  auto BeginSubMesh(std::string name,
    std::shared_ptr<const MaterialAsset> material) -> SubMeshBuilder
  {
    return SubMeshBuilder(*this, std::move(name), std::move(material));
  }

  //! Finalizes a submesh and adds it to the mesh. Only callable with a finished
  //! SubMeshBuilder.
  auto EndSubMesh(SubMeshBuilder&& submesh_builder) -> MeshBuilder&
  {
    if (!submesh_builder.HasMeshViews()) {
      throw std::logic_error("SubMesh must have at least one MeshView");
    }
    submeshes_.emplace_back(SubMeshSpec {
      .name = submesh_builder.Name(),
      .material = submesh_builder.Material(),
      .mesh_views = submesh_builder.MeshViews(),
      .desc = submesh_builder.desc_,
    });
    return *this;
  }

  //! Builds and returns the immutable Mesh.
  OXGN_DATA_NDAPI auto Build() -> std::shared_ptr<Mesh>;

private:
  uint32_t lod_;
  std::string name_;
  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;

  struct SubMeshSpec {
    std::string name;
    std::shared_ptr<const MaterialAsset> material;
    std::vector<pak::MeshViewDesc> mesh_views;
    std::optional<pak::SubMeshDesc> desc;
  };
  std::vector<SubMeshSpec> submeshes_;
  std::optional<pak::MeshDesc> desc_;

  friend class SubMeshBuilder;
  void AddSubMeshFromBuilder(const SubMeshBuilder& builder)
  {
    submeshes_.emplace_back(SubMeshSpec {
      .name = builder.Name(),
      .material = builder.Material(),
      .mesh_views = builder.MeshViews(),
      .desc = builder.desc_,
    });
  }
};

inline auto SubMeshBuilder::EndSubMesh() -> MeshBuilder&
{
  if (!HasMeshViews()) {
    throw std::logic_error("SubMesh must have at least one MeshView");
  }
  parent_.get().AddSubMeshFromBuilder(*this);
  return parent_;
}

} // namespace oxygen::data
