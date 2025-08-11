//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

// Forward declarations
class BufferResource;
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
// Forward declare index view types for MeshView signature
namespace detail {
  struct IndexBufferView;
}

class MeshView {
public:
  OXGN_DATA_API MeshView(const Mesh& mesh, pak::MeshViewDesc desc) noexcept;

  ~MeshView() = default;

  OXYGEN_DEFAULT_COPYABLE(MeshView)
  OXYGEN_DEFAULT_MOVABLE(MeshView)

  OXGN_DATA_NDAPI auto Vertices() const noexcept -> std::span<const Vertex>;

  //! Returns the (possibly sliced) index buffer view for this mesh view.
  /*!
   Returned view shares storage with the parent Mesh and is always zero-copy.
   If the mesh has no index buffer or this view references zero indices, the
   returned view has type IndexType::kNone and empty bytes span.
  */
  OXGN_DATA_NDAPI auto IndexBuffer() const noexcept -> detail::IndexBufferView;

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

  //! Returns the minimum corner of the submesh's axis-aligned bounding box.
  [[nodiscard]] auto BoundingBoxMin() const noexcept -> const glm::vec3&
  {
    return bbox_min_;
  }

  //! Returns the maximum corner of the submesh's axis-aligned bounding box.
  [[nodiscard]] auto BoundingBoxMax() const noexcept -> const glm::vec3&
  {
    return bbox_max_;
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

//=== Buffer Storage Strategies
//===--------------------------------------------//

namespace detail {

  //=== Index Buffer Types ===------------------------------------------------//

  //! Index element type used by meshes.
  enum class IndexType : std::uint8_t {
    kNone = 0, //!< No indices present
    kUInt16, //!< 16-bit unsigned indices
    kUInt32, //!< 32-bit unsigned indices
  };

  //! Lightweight, zero-copy view of an index buffer.
  /*!
   Provides typed and generic access to index data without allocations.
   The underlying storage is exposed as bytes plus an explicit IndexType.
   Helper methods allow accessing as the native span or iterating widened to
   32-bit values (on-the-fly promotion for 16-bit indices, no buffering).
  */
  struct IndexBufferView {
    std::span<const std::byte> bytes; //!< Raw byte span of indices
    IndexType type { IndexType::kNone }; //!< Element type

    [[nodiscard]] constexpr auto Empty() const noexcept -> bool
    {
      return bytes.empty() || type == IndexType::kNone;
    }

    [[nodiscard]] constexpr auto ElementSize() const noexcept -> std::size_t
    {
      switch (type) {
      case IndexType::kUInt16:
        return 2;
      case IndexType::kUInt32:
        return 4;
      default:
        return 0;
      }
    }

    [[nodiscard]] constexpr auto Count() const noexcept -> std::size_t
    {
      auto es = ElementSize();
      return es == 0 ? 0 : bytes.size() / es;
    }

    [[nodiscard]] auto AsU16() const noexcept -> std::span<const std::uint16_t>
    {
      if (type != IndexType::kUInt16)
        return {};
      return { reinterpret_cast<const std::uint16_t*>(bytes.data()), Count() };
    }

    [[nodiscard]] auto AsU32() const noexcept -> std::span<const std::uint32_t>
    {
      if (type != IndexType::kUInt32)
        return {};
      return { reinterpret_cast<const std::uint32_t*>(bytes.data()), Count() };
    }

    // Widened iteration (always yields uint32_t)
    // --------------------------------
    struct WidenedIterator {
      const std::byte* data { nullptr };
      IndexType type { IndexType::kNone };
      std::size_t index { 0 };
      [[nodiscard]] auto operator*() const noexcept -> std::uint32_t
      {
        if (type == IndexType::kUInt16) {
          auto p = reinterpret_cast<const std::uint16_t*>(data);
          return static_cast<std::uint32_t>(p[index]);
        }
        auto p = reinterpret_cast<const std::uint32_t*>(data);
        return p[index];
      }
      auto operator++() noexcept -> WidenedIterator&
      {
        ++index;
        return *this;
      }
      [[nodiscard]] friend auto operator==(
        const WidenedIterator& a, const WidenedIterator& b) noexcept -> bool
      {
        return a.index == b.index;
      }
    };

    struct WidenedRange {
      const std::byte* data { nullptr };
      IndexType type { IndexType::kNone };
      std::size_t count { 0 };
      [[nodiscard]] auto begin() const noexcept -> WidenedIterator
      {
        return { data, type, 0 };
      }
      [[nodiscard]] auto end() const noexcept -> WidenedIterator
      {
        return { data, type, count };
      }
    };

    [[nodiscard]] auto Widened() const noexcept -> WidenedRange
    {
      return WidenedRange { bytes.data(), type, Count() };
    }

    // Slice without copying (byte-range must align with element size).
    [[nodiscard]] auto SliceElements(
      std::size_t first, std::size_t count) const noexcept -> IndexBufferView
    {
      auto es = ElementSize();
      auto byte_off = first * es;
      auto byte_len = count * es;
      if (byte_off > bytes.size() || byte_off + byte_len > bytes.size()) {
        return {}; // invalid slice -> empty
      }
      return IndexBufferView { bytes.subspan(byte_off, byte_len), type };
    }
  };

  //! Storage for meshes that own their vertex/index data (procedural meshes)
  struct OwnedBufferStorage {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices; // always 32-bit for owned storage

    [[nodiscard]] auto GetVertices() const noexcept -> std::span<const Vertex>
    {
      return vertices;
    }

    [[nodiscard]] auto BuildIndexBufferView() const noexcept -> IndexBufferView
    {
      if (indices.empty())
        return {};
      auto raw = std::span<const std::uint32_t>(indices.data(), indices.size());
      auto bytes = std::as_bytes(raw);
      return IndexBufferView { bytes, IndexType::kUInt32 };
    }
  };

  //! Storage for meshes that reference external buffer resources (asset meshes)
  struct ReferencedBufferStorage {
    std::shared_ptr<BufferResource> vertex_buffer_resource;
    std::shared_ptr<BufferResource> index_buffer_resource;

    mutable IndexType cached_index_type { IndexType::kNone };
    mutable bool initialized { false };

    [[nodiscard]] auto GetVertices() const noexcept -> std::span<const Vertex>
    {
      if (!vertex_buffer_resource)
        return {};
      auto data = vertex_buffer_resource->GetData();
      return { reinterpret_cast<const Vertex*>(data.data()),
        data.size() / sizeof(Vertex) };
    }

    OXGN_DATA_API void InitializeIndexInfo() const noexcept;

    [[nodiscard]] auto BuildIndexBufferView() const noexcept -> IndexBufferView
    {
      InitializeIndexInfo();
      if (cached_index_type == IndexType::kNone || !index_buffer_resource)
        return {};
      auto data_u8 = index_buffer_resource->GetData();
      auto bytes_span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data_u8.data()), data_u8.size());
      return IndexBufferView { bytes_span, cached_index_type };
    }
  };

  //! Variant that can hold either owned or referenced buffer data
  using BufferStorage
    = std::variant<OwnedBufferStorage, ReferencedBufferStorage>;

} // namespace detail

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
    return std::visit([](const auto& storage) { return storage.GetVertices(); },
      buffer_storage_);
  }

  //! Returns the index buffer view (may be empty / kNone).
  [[nodiscard]] auto IndexBuffer() const noexcept -> detail::IndexBufferView
  {
    return std::visit(
      [](const auto& storage) { return storage.BuildIndexBufferView(); },
      buffer_storage_);
  }

  //! Returns the number of vertices.
  [[nodiscard]] auto VertexCount() const noexcept -> std::size_t
  {
    return Vertices().size();
  }

  //! Returns the number of indices.
  [[nodiscard]] auto IndexCount() const noexcept -> std::size_t
  {
    return IndexBuffer().Count();
  }

  //! Returns true if the mesh uses an index buffer (i.e., has indices).
  [[nodiscard]] auto IsIndexed() const noexcept -> bool
  {
    return IndexCount() != 0;
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

  // For buffer resource-based meshes
  OXGN_DATA_API Mesh(uint32_t lod,
    std::shared_ptr<BufferResource> vertex_buffer,
    std::shared_ptr<BufferResource> index_buffer);

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

  detail::BufferStorage buffer_storage_;

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

//=== MeshBuilder ===---------------------------------------------------------//

//! Builder for constructing immutable Mesh objects with submeshes and views.
/*!
  MeshBuilder provides a fluent, type-safe API for assembling a Mesh and its
  submeshes/views. It accumulates geometry and submesh definitions, then
  produces a fully immutable Mesh instance. Submesh construction is enforced via
  the SubMeshBuilder type-state pattern.

  ### Storage Type Validation

  MeshBuilder enforces consistent storage usage throughout the build process:
  - **Owned Storage**: Use `WithVertices()` and `WithIndices()` for procedural
    meshes
  - **Referenced Storage**: Use `WithBufferResources()` for asset-loaded meshes

  Once any storage method is called, the builder locks to that storage type.
  Attempting to mix storage types will throw `std::logic_error` with a
  descriptive error message.

  ### Usage Examples

  ```cpp
  // Procedural mesh (owned storage)
  auto mesh = MeshBuilder()
    .WithVertices(vertex_data)
    .WithIndices(index_data)
    .BeginSubMesh("default", material)
      .WithMeshView(view_desc)
    .EndSubMesh()
    .Build();

  // Asset mesh (referenced storage)
  auto mesh = MeshBuilder()
    .WithBufferResources(vertex_buffer, index_buffer)
    .BeginSubMesh("default", material)
      .WithMeshView(view_desc)
    .EndSubMesh()
    .Build();
  ```

  @warning Do not mix `WithVertices/WithIndices` and `WithBufferResources` on
  the same builder instance.

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
    if (submesh_in_progress_) {
      throw std::logic_error(
        "Cannot change storage while a SubMesh is in progress (call EndSubMesh "
        "before modifying storage)");
    }
    ValidateStorageType(StorageType::kOwned);
    vertices_.assign(vertices.begin(), vertices.end());
    using_owned_storage_ = true;
    storage_type_ = StorageType::kOwned;
    return *this;
  }

  //! Sets the mesh indices (replaces any existing indices).
  auto WithIndices(std::span<const std::uint32_t> indices) -> MeshBuilder&
  {
    if (submesh_in_progress_) {
      throw std::logic_error(
        "Cannot change storage while a SubMesh is in progress (call EndSubMesh "
        "before modifying storage)");
    }
    ValidateStorageType(StorageType::kOwned);
    indices_.assign(indices.begin(), indices.end());
    using_owned_storage_ = true;
    storage_type_ = StorageType::kOwned;
    return *this;
  }

  //! Sets the mesh to reference external buffer resources (for asset-loaded
  //! meshes).
  auto WithBufferResources(std::shared_ptr<BufferResource> vertex_buffer,
    std::shared_ptr<BufferResource> index_buffer) -> MeshBuilder&
  {
    if (submesh_in_progress_) {
      throw std::logic_error(
        "Cannot change storage while a SubMesh is in progress (call EndSubMesh "
        "before modifying storage)");
    }
    ValidateStorageType(StorageType::kReferenced);
    vertex_buffer_resource_ = std::move(vertex_buffer);
    index_buffer_resource_ = std::move(index_buffer);
    using_owned_storage_ = false;
    storage_type_ = StorageType::kReferenced;
    return *this;
  }

  auto WithDescriptor(pak::MeshDesc desc) -> MeshBuilder&
  {
    desc_ = std::move(desc);
    return *this;
  }

  //! Returns true if the builder is using owned storage (vertices/indices).
  [[nodiscard]] auto IsUsingOwnedStorage() const noexcept -> bool
  {
    return storage_type_ == StorageType::kOwned;
  }

  //! Returns true if the builder is using referenced storage (BufferResources).
  [[nodiscard]] auto IsUsingReferencedStorage() const noexcept -> bool
  {
    return storage_type_ == StorageType::kReferenced;
  }

  //! Returns true if no storage type has been configured yet.
  [[nodiscard]] auto IsStorageUninitialized() const noexcept -> bool
  {
    return storage_type_ == StorageType::kUninitialized;
  }

  //! Begins a new submesh definition. Returns a SubMeshBuilder for mesh view
  //! accumulation.
  auto BeginSubMesh(std::string name,
    std::shared_ptr<const MaterialAsset> material) -> SubMeshBuilder
  {
    if (submesh_in_progress_) {
      throw std::logic_error(
        "Cannot begin a new SubMesh while another SubMesh is in progress (did "
        "you forget to call EndSubMesh?)");
    }
    if (!material) {
      throw std::logic_error("SubMesh material must not be null");
    }
    submesh_in_progress_ = true;
    return SubMeshBuilder(*this, std::move(name), std::move(material));
  }

  //! Finalizes a submesh and adds it to the mesh. Only callable with a finished
  //! SubMeshBuilder.
  auto EndSubMesh(SubMeshBuilder&& submesh_builder) -> MeshBuilder&
  {
    if (!submesh_in_progress_) {
      throw std::logic_error(
        "Cannot end a SubMesh when none has been begun (no active SubMesh)");
    }
    if (!submesh_builder.HasMeshViews()) {
      throw std::logic_error("SubMesh must have at least one MeshView");
    }
    submeshes_.emplace_back(SubMeshSpec {
      .name = submesh_builder.Name(),
      .material = submesh_builder.Material(),
      .mesh_views = submesh_builder.MeshViews(),
      .desc = submesh_builder.desc_,
    });
    submesh_in_progress_ = false;
    return *this;
  }

  //! Builds and returns the immutable Mesh.
  OXGN_DATA_NDAPI auto Build() -> std::unique_ptr<Mesh>;

private:
  //! Storage type tracking for validation
  enum class StorageType {
    kUninitialized, //!< No storage type set yet
    kOwned, //!< Uses owned storage (vertices/indices vectors)
    kReferenced //!< Uses referenced storage (BufferResource pointers)
  };

  //! Validates that the requested storage type is compatible with current
  //! state.
  /*!
   @param requested_type The storage type being requested
   @throw std::logic_error if attempting to mix storage types
  */
  void ValidateStorageType(StorageType requested_type)
  {
    if (storage_type_ == StorageType::kUninitialized) {
      // First time setting storage type - allow any type
      return;
    }

    if (storage_type_ != requested_type) {
      const char* current_type_name = (storage_type_ == StorageType::kOwned)
        ? "owned storage (WithVertices/WithIndices)"
        : "referenced storage (WithBufferResources)";
      const char* requested_type_name = (requested_type == StorageType::kOwned)
        ? "owned storage (WithVertices/WithIndices)"
        : "referenced storage (WithBufferResources)";

      throw std::logic_error(
        fmt::format("Cannot mix storage types: mesh is already configured for "
                    "{} but {} was requested",
          current_type_name, requested_type_name));
    }
  }

  uint32_t lod_;
  std::string name_;

  // Storage type tracking
  StorageType storage_type_ = StorageType::kUninitialized;

  // For owned storage (procedural meshes)
  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;

  // For referenced storage (asset meshes)
  std::shared_ptr<BufferResource> vertex_buffer_resource_;
  std::shared_ptr<BufferResource> index_buffer_resource_;

  // Tracks which storage type to use
  bool using_owned_storage_ = true;

  struct SubMeshSpec {
    std::string name;
    std::shared_ptr<const MaterialAsset> material;
    std::vector<pak::MeshViewDesc> mesh_views;
    std::optional<pak::SubMeshDesc> desc;
  };
  std::vector<SubMeshSpec> submeshes_;
  std::optional<pak::MeshDesc> desc_;
  bool submesh_in_progress_
    = false; //!< Tracks an active (unfinalized) SubMeshBuilder

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
  if (!parent_.get().submesh_in_progress_) {
    throw std::logic_error(
      "Cannot end a SubMesh when none has been begun (no active SubMesh)");
  }
  if (!HasMeshViews()) {
    throw std::logic_error("SubMesh must have at least one MeshView");
  }
  parent_.get().AddSubMeshFromBuilder(*this);
  parent_.get().submesh_in_progress_ = false;
  return parent_;
}

} // namespace oxygen::data
