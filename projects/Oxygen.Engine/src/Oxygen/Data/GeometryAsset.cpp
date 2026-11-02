//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/LodPolicyValidation.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Data/Vertex.h>

using oxygen::data::BufferResource;
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;
using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;
using oxygen::data::detail::IndexBufferView;
using oxygen::data::detail::IndexType;
using oxygen::data::detail::ReferencedBufferStorage;

namespace {

//! The type of the indices `buffer` holds, which its format names, or its
//! stride for a buffer without one.
auto IndexTypeOf(const BufferResource& buffer) -> IndexType
{
  const auto format = buffer.GetElementFormat();
  const auto unformatted = format == oxygen::Format::kUnknown;
  const auto stride = buffer.GetElementStride();
  const auto narrow
    = format == oxygen::Format::kR16UInt || (unformatted && stride == 2U);
  const auto wide
    = format == oxygen::Format::kR32UInt || (unformatted && stride == 4U);
  if (!narrow && !wide) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      "The index buffer does not hold 16-bit or 32-bit indices");
  }
  const auto element_size
    = narrow ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
  if (buffer.GetData().size() % element_size != 0) {
    throw CookedOpenError(CookedDefect::kWrongRowSize,
      "The index buffer does not hold whole indices");
  }
  return narrow ? IndexType::kUInt16 : IndexType::kUInt32;
}

//! Checks that `buffer` holds whole vertices, aligned for them.
auto CheckVertexBuffer(const BufferResource& buffer) -> void
{
  const auto bytes = buffer.GetData();
  if (bytes.size() % sizeof(Vertex) != 0) {
    throw CookedOpenError(CookedDefect::kWrongRowSize,
      "The vertex buffer does not hold whole vertices");
  }
  if (std::bit_cast<std::uintptr_t>(bytes.data()) % alignof(Vertex) != 0) {
    throw CookedOpenError(CookedDefect::kMisaligned,
      "The vertex buffer is not aligned for its vertices");
  }
}

//! Checks that buffers of `vertex_count` vertices and of `indices` hold the
//! ranges of `view`, and that each of its indices, which count from its
//! first vertex, names one of its vertices.
auto CheckView(const MeshView& view, const std::size_t vertex_count,
  const IndexBufferView& indices) -> void
{
  // Subtracting from the buffer sizes, which a range cannot exceed, does not
  // wrap as adding a range's start and count can.
  const auto holds_vertices = view.FirstVertex() <= vertex_count
    && view.VertexCount() <= vertex_count - view.FirstVertex();
  const auto holds_indices = view.FirstIndex() <= indices.Count()
    && view.IndexCount() <= indices.Count() - view.FirstIndex();
  if (!holds_vertices || !holds_indices) {
    throw CookedOpenError(CookedDefect::kOutOfBounds,
      fmt::format("The mesh view of {} vertices from {} and {} indices from {} "
                  "is past its buffers of {} vertices and {} indices",
        view.VertexCount(), view.FirstVertex(), view.IndexCount(),
        view.FirstIndex(), vertex_count, indices.Count()));
  }
  const auto drawn
    = indices.SliceElements(view.FirstIndex(), view.IndexCount());
  const auto in_view = std::ranges::all_of(
    drawn.Widened(), [&view](const std::uint32_t index) -> bool {
      return index < view.VertexCount();
    });
  if (!in_view) {
    throw CookedOpenError(CookedDefect::kOutOfBounds,
      fmt::format("An index of the mesh view is past its {} vertices",
        view.VertexCount()));
  }
}

} // namespace

MeshView::MeshView(const Mesh& mesh, pak::geometry::MeshViewDesc desc)
  : mesh_(mesh)
  , desc_(desc)
{
  if (desc_.vertex_count == 0 || desc_.index_count == 0) {
    throw std::invalid_argument(
      "MeshView must draw at least one vertex and one index");
  }
  // A mesh read from its descriptor checks its views when its buffers are
  // bound.
  if (mesh.HasBuffers()) {
    CheckView(*this, mesh.Vertices().size(), mesh.IndexBuffer());
  }
}

auto MeshView::Vertices() const noexcept -> std::span<const Vertex>
{
  if (!mesh_.get().HasBuffers()) {
    return {};
  }
  return mesh_.get().Vertices().subspan(desc_.first_vertex, desc_.vertex_count);
}

auto MeshView::IndexBuffer() const noexcept -> detail::IndexBufferView
{
  if (!mesh_.get().HasBuffers()) {
    return {};
  }
  return mesh_.get().IndexBuffer().SliceElements(
    desc_.first_index, desc_.index_count);
}

auto MeshView::TakeBoundsOf(const SubMesh& submesh) noexcept -> void
{
  const auto& min = submesh.BoundingBoxMin();
  const auto& max = submesh.BoundingBoxMax();
  for (glm::length_t axis = 0; axis < 3; ++axis) {
    desc_.bounding_box_min[axis] = min[axis];
    desc_.bounding_box_max[axis] = max[axis];
  }
}

SubMesh::SubMesh(const Mesh& mesh, std::string name,
  std::shared_ptr<const MaterialAsset> material)
  : mesh_(mesh)
  , name_(std::move(name))
  , material_(std::move(material))
{
  if (!material_) {
    throw std::logic_error("SubMesh material must not be null");
  }
}

Mesh::Mesh(const LodIndex lod, std::vector<Vertex> vertices,
  std::vector<std::uint32_t> indices)
  : name_(fmt::format("LOD_{}", lod))
  , buffer_storage_(detail::OwnedBufferStorage {
      .vertices = std::move(vertices),
      .indices = std::move(indices),
    })
{
  if (std::get<detail::OwnedBufferStorage>(buffer_storage_).vertices.empty()) {
    throw std::invalid_argument("Mesh must have at least one vertex");
  }
  ComputeBounds();
}

Mesh::Mesh(const LodIndex lod, std::shared_ptr<BufferResource> vertex_buffer,
  std::shared_ptr<BufferResource> index_buffer)
  : name_(fmt::format("LOD_{}", lod))
  , buffer_storage_(detail::ReferencedBufferStorage {})
{
  // A mesh read from its descriptor is built before its buffers load, and
  // they are bound to it then.
  if (vertex_buffer || index_buffer) {
    SetBufferResources(std::move(vertex_buffer), std::move(index_buffer));
    ComputeBounds();
  }
}

auto Mesh::SetBufferResources(std::shared_ptr<BufferResource> vertex_buffer,
  std::shared_ptr<BufferResource> index_buffer) -> void
{
  auto* referenced
    = std::get_if<detail::ReferencedBufferStorage>(&buffer_storage_);
  if (referenced == nullptr) {
    throw std::logic_error("A mesh that owns its vertices binds no buffers");
  }
  if (!vertex_buffer || !index_buffer) {
    throw CookedOpenError(CookedDefect::kMissingPart,
      "The mesh is bound without its vertex or its index buffer");
  }
  CheckVertexBuffer(*vertex_buffer);
  const auto index_type = IndexTypeOf(*index_buffer);
  const auto vertex_count = vertex_buffer->GetData().size() / sizeof(Vertex);
  const auto indices = IndexBufferView {
    .bytes = std::as_bytes(index_buffer->GetData()),
    .type = index_type,
  };
  for (const auto& submesh : submeshes_) {
    for (const auto& view : submesh.MeshViews()) {
      CheckView(view, vertex_count, indices);
    }
  }
  referenced->vertex_buffer_resource = std::move(vertex_buffer);
  referenced->index_buffer_resource = std::move(index_buffer);
  referenced->index_type = index_type;
}

//! Computes bounding box and sphere - the single source of truth.
/*!
  Uses the authored bounds for standard/skinned PAK descriptors, including
  valid all-zero bounds. Procedural descriptors contain recipe data rather
  than bounds, so their bounds come from the generated vertices, as they do
  for meshes without a descriptor. Empty vertex storage yields zero bounds.

  Data members (bbox_min_, bbox_max_, bounding_sphere_) are the source of truth.
*/
auto Mesh::ComputeBounds() -> void
{
  // Step 1: Compute or copy bounding box
  if (desc_.has_value() && !desc_->IsProcedural()) {
    const auto& desc = desc_.value();
    if (desc.IsStandard()) {
      const auto& standard = desc.info.standard;
      bbox_min_ = glm::vec3(standard.bounding_box_min[0],
        standard.bounding_box_min[1], standard.bounding_box_min[2]);
      bbox_max_ = glm::vec3(standard.bounding_box_max[0],
        standard.bounding_box_max[1], standard.bounding_box_max[2]);
    } else if (desc.IsSkinned()) {
      const auto& skinned = desc.info.skinned;
      bbox_min_ = glm::vec3(skinned.bounding_box_min[0],
        skinned.bounding_box_min[1], skinned.bounding_box_min[2]);
      bbox_max_ = glm::vec3(skinned.bounding_box_max[0],
        skinned.bounding_box_max[1], skinned.bounding_box_max[2]);
    } else {
      bbox_min_ = bbox_max_ = glm::vec3(0.0F);
    }
  } else {
    const auto vertices = Vertices();
    if (vertices.empty()) {
      bbox_min_ = bbox_max_ = glm::vec3(0.0F);
    } else {
      bbox_min_ = bbox_max_ = vertices.front().position;
      for (const auto& vertex : vertices.subspan(1)) {
        bbox_min_ = glm::min(bbox_min_, vertex.position);
        bbox_max_ = glm::max(bbox_max_, vertex.position);
      }
    }
  }

  // Step 2: Always compute bounding sphere from bounding box
  const glm::vec3 center = (bbox_min_ + bbox_max_) * 0.5F;
  const float radius = glm::length(bbox_max_ - center);
  bounding_sphere_ = glm::vec4(center, radius);
}

//! Computes bounding box and sphere for SubMesh - the single source of truth.
/*!
  Computes bounding data from the PAK descriptor when available.
  If no descriptor is present, bounds remain zero-initialized.

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
    bool has_vertices = false;
    for (const auto& mesh_view : mesh_views_) {
      for (const auto& vertex : mesh_view.Vertices()) {
        if (!has_vertices) {
          bbox_min_ = bbox_max_ = vertex.position;
          has_vertices = true;
        } else {
          bbox_min_ = glm::min(bbox_min_, vertex.position);
          bbox_max_ = glm::max(bbox_max_, vertex.position);
        }
      }
    }

    if (!has_vertices) {
      bbox_min_ = bbox_max_ = glm::vec3(0.0F);
    }
    // Views built without cooked bounds are bounded conservatively.
    for (auto& mesh_view : mesh_views_) {
      mesh_view.TakeBoundsOf(*this);
    }
  }

  // Step 2: Always compute bounding sphere from bounding box
  const glm::vec3 center = (bbox_min_ + bbox_max_) * 0.5F;
  const float radius = glm::length(bbox_max_ - center);
  bounding_sphere_ = glm::vec4(center, radius);
}

using oxygen::data::MeshBuilder;
using oxygen::data::SurfaceId;

oxygen::data::GeometryAsset::GeometryAsset(AssetKey asset_key,
  pak::geometry::GeometryAssetDesc desc,
  std::vector<std::shared_ptr<Mesh>> lod_meshes, SourceOrigin source_origin,
  const std::optional<std::span<const pak::geometry::SurfaceDesc>>
    surface_table)
  : Asset(asset_key, source_origin)
  , desc_(desc)
  , lod_meshes_(std::move(lod_meshes))
{
  surfaces_.geometry_asset_key = asset_key;
  if (lod_meshes_.size() > pak::geometry::kMaxLodCount) {
    throw std::invalid_argument("Geometry has too many LODs");
  }
  std::unordered_map<SurfaceId, size_t> surface_indices;
  for (size_t lod = 0; lod < lod_meshes_.size(); ++lod) {
    const auto& mesh = lod_meshes_.at(lod);
    if (!mesh) {
      throw std::invalid_argument("Geometry LOD must not be null");
    }
    const auto submeshes = mesh->SubMeshes();
    if (submeshes.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::invalid_argument("Geometry has too many submeshes");
    }
    for (size_t index = 0; index < submeshes.size(); ++index) {
      const auto& submesh = oxygen::base::CheckedAt(submeshes, index);
      const auto id = submesh.SurfaceIdentity();
      if (id.IsNil()) {
        throw std::invalid_argument(
          "Geometry surface identity must not be nil");
      }
      const auto [location, inserted]
        = surface_indices.try_emplace(id, surfaces_.surfaces.size());
      if (inserted) {
        surfaces_.surfaces.push_back(Surface {
          .surface_id = id,
          .display_name = std::string(submesh.GetName()),
          .bindings = {},
          .defaults = std::nullopt,
        });
      }
      auto key = AssetKey {};
      if (submesh.Descriptor()) {
        key = submesh.Descriptor()->material_asset_key;
      } else if (submesh.Material()) {
        key = submesh.Material()->GetAssetKey();
      }
      surfaces_.surfaces.at(location->second)
        .bindings.push_back(SurfaceBinding {
          .lod_index = LodIndex { static_cast<uint32_t>(lod) },
          .submesh_index = SubmeshIndex { static_cast<uint32_t>(index) },
          .default_material_key = key,
        });
    }
  }
  const auto revision = ComputeSurfaceLayoutRevision(surfaces_.surfaces);
  if (!revision) {
    throw std::invalid_argument("Geometry surface layout is invalid");
  }
  surfaces_.layout_revision = revision.value();
  if (surface_table.has_value()) {
    ApplySurfaceTable(*surface_table);
  }
}

auto oxygen::data::GeometryAsset::ApplySurfaceTable(
  const std::span<const pak::geometry::SurfaceDesc> surface_table) -> void
{
  if (surface_table.size() != surfaces_.surfaces.size()) {
    throw std::invalid_argument(
      "Geometry surface table does not list the declared surfaces");
  }
  constexpr auto kKnownFlags = pak::geometry::kSurfaceFlag_Visible
    | pak::geometry::kSurfaceFlag_CastsShadows;
  for (const auto& entry : surface_table) {
    if ((entry.flags & ~kKnownFlags) != 0U) {
      throw std::invalid_argument("Geometry surface flags are unknown");
    }
    const auto found = std::ranges::find(
      surfaces_.surfaces, entry.surface_id, &Surface::surface_id);
    if (found == surfaces_.surfaces.end() || found->defaults.has_value()) {
      throw std::invalid_argument(
        "Geometry surface table does not list the declared surfaces");
    }
    found->defaults = SurfaceDefaults {
      .visible = (entry.flags & pak::geometry::kSurfaceFlag_Visible) != 0U,
      .casts_shadows
      = (entry.flags & pak::geometry::kSurfaceFlag_CastsShadows) != 0U,
    };
  }
  if (!IsValidLodPolicy(desc_.default_lod_policy)) {
    throw std::invalid_argument("Geometry default LOD policy is invalid");
  }
  default_lod_policy_ = desc_.default_lod_policy;
}

auto oxygen::data::GeometryAsset::FindSurface(const SurfaceId id) const noexcept
  -> const Surface*
{
  const auto found
    = std::ranges::find(surfaces_.surfaces, id, &Surface::surface_id);
  return found == surfaces_.surfaces.end() ? nullptr : &*found;
}

//! Builds and returns the immutable Mesh.
auto MeshBuilder::Build() -> std::unique_ptr<Mesh>
{
  if (submesh_in_progress_) {
    throw std::logic_error(
      "Cannot build Mesh while a SubMesh is in progress (active SubMesh not "
      "ended)");
  }
  if (submeshes_.empty()) {
    throw std::logic_error("Mesh must have at least one submesh");
  }

  // Create the Mesh object using the appropriate constructor
  std::unique_ptr<Mesh> mesh;
  if (using_owned_storage_) {
    // Use owned storage constructor (procedural meshes)
    mesh = std::unique_ptr<Mesh>(
      new Mesh(lod_, std::move(vertices_), std::move(indices_)));
  } else {
    // Use referenced storage constructor (asset meshes); a mesh read from
    // its descriptor has its buffers bound after it loads.
    mesh = std::unique_ptr<Mesh>(
      new Mesh(lod_, vertex_buffer_resource_, index_buffer_resource_));
  }

  mesh->SetName(name_);

  // Set mesh descriptor if provided
  if (desc_.has_value()) {
    mesh->SetDescriptor(desc_.value());
  }

  if (mesh->IsSkinned()) {
    mesh->SetSkiningBufferResources(std::move(joint_index_buffer_resource_),
      std::move(joint_weight_buffer_resource_),
      std::move(inverse_bind_buffer_resource_),
      std::move(joint_remap_buffer_resource_));
  }
  // For each submesh spec, create MeshViews and SubMesh, then add to mesh
  for (const auto& spec : submeshes_) {
    SubMesh submesh(*mesh, spec.name, spec.material);

    if (spec.mesh_views.empty()) {
      throw std::logic_error("SubMesh must have at least one MeshView");
    }

    // Set submesh descriptor if provided
    if (spec.desc.has_value()) {
      submesh.SetDescriptor(spec.desc.value());
    } else {
      submesh.surface_id_
        = spec.surface_id.IsNil() ? SurfaceId::Generate() : spec.surface_id;
    }

    for (const auto& view_desc : spec.mesh_views) {
      submesh.AddMeshViewInternal(view_desc);
    }
    // Compute bounds (descriptor-provided or computed from mesh views) before
    // adding
    submesh.ComputeBounds();
    mesh->AddSubMeshInternal(std::move(submesh));
  }

  return mesh;
}
