//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MeshType.h>

using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;
using oxygen::data::detail::IndexType;
using oxygen::data::detail::ReferencedBufferStorage;

namespace {

// Helper function for converting PAK name arrays to strings
// TODO: This duplicates Asset::GetAssetName() logic - consider consolidation
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

namespace oxygen::data::detail {

void ReferencedBufferStorage::InitializeIndexInfo() const noexcept
{
  if (initialized)
    return;
  initialized = true;
  if (!index_buffer_resource) {
    cached_index_type = IndexType::kNone;
    return;
  }
  auto fmt = index_buffer_resource->GetElementFormat();
  if (fmt == Format::kR16UInt) {
    cached_index_type = IndexType::kUInt16;
  } else if (fmt == Format::kR32UInt) {
    cached_index_type = IndexType::kUInt32;
  } else if (fmt == Format::kUnknown) {
    // Fall back to element stride
    auto stride = index_buffer_resource->GetElementStride();
    if (stride == 2)
      cached_index_type = IndexType::kUInt16;
    else if (stride == 4)
      cached_index_type = IndexType::kUInt32;
    else {
      LOG_F(ERROR, "Unsupported raw index stride (must be 2 or 4)");
      cached_index_type = IndexType::kNone;
    }
  } else {
    LOG_F(ERROR, "Unsupported index format (only R16UInt/R32UInt)");
    cached_index_type = IndexType::kNone;
  }
  // Validate size alignment
  auto bytes = index_buffer_resource->GetData().size();
  auto es = cached_index_type == IndexType::kUInt16
    ? 2
    : (cached_index_type == IndexType::kUInt32 ? 4 : 1);
  if (cached_index_type != IndexType::kNone && bytes % es != 0) {
    LOG_F(ERROR, "Index buffer byte size not multiple of element size");
    cached_index_type = IndexType::kNone;
  }
}

} // namespace oxygen::data::detail

MeshView::MeshView(const Mesh& mesh, pak::MeshViewDesc desc) noexcept
  : mesh_(mesh)
  , desc_(std::move(desc))
{
  // Enforce design constraints
  CHECK_F(desc_.vertex_count > 0, "MeshView must have at least one vertex");
  CHECK_F(desc_.index_count > 0, "MeshView must have at least one index");
  CHECK_F(desc_.first_vertex + desc_.vertex_count <= mesh.Vertices().size(),
    "MeshView vertex range exceeds mesh vertex count");
  const auto ib = mesh.IndexBuffer();
  if (!ib.Empty()) {
    CHECK_F(desc_.first_index + desc_.index_count <= ib.Count(),
      "MeshView index range exceeds mesh index count");
  }
}

auto MeshView::Vertices() const noexcept -> std::span<const Vertex>
{
  return mesh_.get().Vertices().subspan(desc_.first_vertex, desc_.vertex_count);
}

auto MeshView::IndexBuffer() const noexcept -> detail::IndexBufferView
{
  auto full = mesh_.get().IndexBuffer();
  if (full.Empty())
    return {};
  return full.SliceElements(desc_.first_index, desc_.index_count);
}

SubMesh::SubMesh(const Mesh& mesh, std::string name,
  std::shared_ptr<const MaterialAsset> material)
  : mesh_(mesh)
  , name_(std::move(name))
  , material_(std::move(material))
{
  // Enforce design constraints
  CHECK_NOTNULL_F(
    material_, "SubMesh must have exactly one Material (1:1 constraint)");
}

Mesh::Mesh(uint32_t lod, std::vector<Vertex> vertices,
  std::vector<std::uint32_t> indices)
  : name_(fmt::format("LOD_{}", lod))
  , buffer_storage_(detail::OwnedBufferStorage {
      .vertices = std::move(vertices), .indices = std::move(indices) })
{
  auto& owned_storage = std::get<detail::OwnedBufferStorage>(buffer_storage_);
  CHECK_F(
    !owned_storage.vertices.empty(), "Mesh must have at least one vertex");
  ComputeBounds();
}

Mesh::Mesh(uint32_t lod, std::shared_ptr<BufferResource> vertex_buffer,
  std::shared_ptr<BufferResource> index_buffer)
  : name_(fmt::format("LOD_{}", lod))
  , buffer_storage_(detail::ReferencedBufferStorage {
      .vertex_buffer_resource = std::move(vertex_buffer),
      .index_buffer_resource = std::move(index_buffer) })
{
  auto& referenced_storage
    = std::get<detail::ReferencedBufferStorage>(buffer_storage_);
  CHECK_NOTNULL_F(referenced_storage.vertex_buffer_resource,
    "Referenced mesh must have a vertex buffer resource");
  // Index buffer is optional for some mesh types

  auto vertices = referenced_storage.GetVertices();
  CHECK_F(!vertices.empty(), "Mesh must have at least one vertex");
  ComputeBounds();
}

//! Computes bounding box and sphere - the single source of truth.
/*!
  Computes bounding data using the most appropriate method:
  - If PAK descriptor exists: use pre-computed bounding box
  - If no descriptor: compute bounding box from vertices
  - Always compute bounding sphere from the resulting bounding box

  Data members (bbox_min_, bbox_max_, bounding_sphere_) are the source of truth.
*/
auto Mesh::ComputeBounds() -> void
{
  // Step 1: Compute or copy bounding box
  if (desc_.has_value() && desc_.value().IsStandard()) {
    // Use pre-computed bounds from PAK descriptor
    const auto& desc = desc_.value().info.standard;
    bbox_min_ = glm::vec3(desc.bounding_box_min[0], desc.bounding_box_min[1],
      desc.bounding_box_min[2]);
    bbox_max_ = glm::vec3(desc.bounding_box_max[0], desc.bounding_box_max[1],
      desc.bounding_box_max[2]);
  } else {
    // Compute from vertices using the variant storage
    auto vertices = Vertices(); // Get vertices through the variant interface
    if (vertices.empty()) {
      bbox_min_ = bbox_max_ = glm::vec3(0.0f);
    } else {
      bbox_min_ = bbox_max_ = vertices.front().position;
      for (const auto& v : vertices) {
        bbox_min_ = glm::min(bbox_min_, v.position);
        bbox_max_ = glm::max(bbox_max_, v.position);
      }
    }
  }

  // Step 2: Always compute bounding sphere from bounding box
  const glm::vec3 center = (bbox_min_ + bbox_max_) * 0.5f;
  const float radius = glm::length(bbox_max_ - center);
  bounding_sphere_ = glm::vec4(center, radius);
}

//! Computes bounding box and sphere for SubMesh - the single source of truth.
/*!
  Computes bounding data using the most appropriate method:
  - If PAK descriptor exists: use pre-computed bounding box
  - If no descriptor: compute bounding box from mesh view vertices
  - Always compute bounding sphere from the resulting bounding box

  Data members (bbox_min_, bbox_max_, bounding_sphere_) are the source of truth.
*/
auto SubMesh::ComputeBounds() -> void
{
  // Step 1: Compute or copy bounding box
  if (desc_.has_value()) {
    // Use pre-computed bounds from PAK descriptor
    const auto& desc = desc_.value();
    bbox_min_ = glm::vec3(desc.bounding_box_min[0], desc.bounding_box_min[1],
      desc.bounding_box_min[2]);
    bbox_max_ = glm::vec3(desc.bounding_box_max[0], desc.bounding_box_max[1],
      desc.bounding_box_max[2]);
  } else {
    // Compute from mesh view vertices
    if (mesh_views_.empty()) {
      bbox_min_ = bbox_max_ = glm::vec3(0.0f);
    } else {
      bool first = true;
      for (const auto& mesh_view : mesh_views_) {
        for (const auto& vertex : mesh_view.Vertices()) {
          if (first) {
            bbox_min_ = bbox_max_ = vertex.position;
            first = false;
          } else {
            bbox_min_ = glm::min(bbox_min_, vertex.position);
            bbox_max_ = glm::max(bbox_max_, vertex.position);
          }
        }
      }
      if (first) {
        // No vertices found
        bbox_min_ = bbox_max_ = glm::vec3(0.0f);
      }
    }
  }

  // Step 2: Always compute bounding sphere from bounding box
  const glm::vec3 center = (bbox_min_ + bbox_max_) * 0.5f;
  const float radius = glm::length(bbox_max_ - center);
  bounding_sphere_ = glm::vec4(center, radius);
}

using oxygen::data::MeshBuilder;

//! Builds and returns the immutable Mesh.
auto MeshBuilder::Build() -> std::unique_ptr<Mesh>
{
  CHECK_F(!submeshes_.empty(), "Mesh must have at least one submesh");

  // Create the Mesh object using the appropriate constructor
  std::unique_ptr<Mesh> mesh;
  if (using_owned_storage_) {
    // Use owned storage constructor (procedural meshes)
    CHECK_F(!vertices_.empty(), "Mesh must have vertices");
    mesh = std::unique_ptr<Mesh>(
      new Mesh(lod_, std::move(vertices_), std::move(indices_)));
  } else {
    // Use referenced storage constructor (asset meshes)
    CHECK_NOTNULL_F(vertex_buffer_resource_,
      "Referenced mesh must have vertex buffer resource");
    mesh = std::unique_ptr<Mesh>(
      new Mesh(lod_, vertex_buffer_resource_, index_buffer_resource_));
  }

  mesh->SetName(name_);

  // Set mesh descriptor if provided
  if (desc_.has_value()) {
    mesh->SetDescriptor(desc_.value());
  }
  // For each submesh spec, create MeshViews and SubMesh, then add to mesh
  for (const auto& spec : submeshes_) {
    SubMesh submesh(*mesh, spec.name, spec.material);

    // Enforce design constraint: SubMesh must have at least one MeshView
    CHECK_F(!spec.mesh_views.empty(),
      "SubMesh must have at least one MeshView (1:N constraint)");

    // Set submesh descriptor if provided
    if (spec.desc.has_value()) {
      submesh.SetDescriptor(spec.desc.value());
    }

    for (const auto& view_desc : spec.mesh_views) {
      submesh.AddMeshViewInternal(std::move(view_desc));
    }
    mesh->AddSubMeshInternal(std::move(submesh));
  }

  return mesh;
}
