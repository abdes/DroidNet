//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content::loaders {

namespace detail {

  // Accepts any Result<T> and checks for error, logs and throws if needed
  template <typename ResultT>
  auto CheckResult(const ResultT& result, const char* field_name) -> void
  {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field_name,
        result.error().message().c_str());
      throw std::runtime_error(
        fmt::format("error reading geometry asset ({}): {}", field_name,
          result.error().message()));
    }
  }

  // Helper to read a bounding box (float[3])
  inline auto ReadBoundingBox(
    serio::AnyReader& reader, float (&bbox)[3], const char* field_name) -> void
  {
    for (float& i : bbox) {
      auto f_result = reader.ReadInto<float>(i);
      CheckResult(f_result, field_name);
    }
  }

  // Handles loading of vertex/index buffers for standard meshes
  inline auto LoadStandardMeshBuffers(
    LoaderContext& context, data::pak::geometry::MeshDesc& desc)
    -> std::pair<std::shared_ptr<data::BufferResource>,
      std::shared_ptr<data::BufferResource>>
  {
    using data::BufferResource;
    using data::ResourceReferenceIndex;

    DCHECK_NOTNULL_F(
      context.desc_reader, "expecting desc_reader not to be null");
    auto& reader = *context.desc_reader;

    // Access union field via desc.info.standard
    auto& info = desc.info.standard;

    // Read vertex_buffer first (according to StandardMeshInfo layout)
    auto vb_result
      = reader.ReadInto<ResourceReferenceIndex>(info.vertex_buffer);
    CheckResult(vb_result, "m.vertex_buffer");
    LOG_F(2, "vertex buffer   : {}", info.vertex_buffer);

    // Read index_buffer second
    auto ib_result = reader.ReadInto<ResourceReferenceIndex>(info.index_buffer);
    CheckResult(ib_result, "m.index_buffer");
    LOG_F(2, "index buffer    : {}", info.index_buffer);

    // Read bounding boxes after buffer indices
    for (float& i : desc.info.standard.bounding_box_min) {
      auto min_result = reader.ReadInto<float>(i);
      CheckResult(min_result, "m.bounding_box_min");
    }
    for (float& i : desc.info.standard.bounding_box_max) {
      auto max_result = reader.ReadInto<float>(i);
      CheckResult(max_result, "m.bounding_box_max");
    }

    // MeshInfo arms are fixed-width on disk. Consume the reserved tail bytes so
    // subsequent SubMeshDesc reads start at the correct offset.
    constexpr size_t kStandardConsumed
      = (sizeof(ResourceReferenceIndex) * 2) + (sizeof(float) * 3 * 2);
    constexpr size_t kStandardTail
      = sizeof(data::pak::geometry::StandardMeshInfo) - kStandardConsumed;
    static_assert(kStandardTail == 40);
    auto skip_result
      = reader.Forward(static_cast<std::streamoff>(kStandardTail));
    CheckResult(skip_result, "m.standard.reserved");

    if (context.parse_only) {
      return { nullptr, nullptr };
    }

    if (!context.dependency_collector) {
      LOG_F(ERROR,
        "GeometryLoader requires a DependencyCollector for non-parse-only "
        "loads (standard mesh buffers)");
      throw std::runtime_error(
        "GeometryLoader requires a DependencyCollector for async decode");
    }

    auto collect_buffer_ref
      = [&](const ResourceReferenceIndex reference) -> void {
      const auto resource_index
        = context.ResolveResource(reference, data::ResourceKind::kBuffer);
      if (!resource_index) {
        return;
      }
      internal::ResourceRef ref {
        .source = context.source_instance,
        .resource_type_id = BufferResource::ClassTypeId(),
        .resource_index = *resource_index,
      };
      context.dependency_collector->AddResourceDependency(ref);
    };

    collect_buffer_ref(info.vertex_buffer);
    collect_buffer_ref(info.index_buffer);
    return { nullptr, nullptr };
  }

  // Handles loading and generation for procedural meshes
  inline auto LoadProceduralMeshBuffers(serio::AnyReader& reader,
    data::pak::geometry::MeshDesc& desc, const std::string& name,
    std::vector<data::Vertex>& vertices, std::vector<uint32_t>& indices) -> void
  {
    // Access union field via desc.info.procedural
    auto& info = desc.info.procedural;

    // Read params_size from the stream
    auto params_size_result = reader.ReadInto<uint32_t>(info.params_size);
    CheckResult(params_size_result, "m.param_blob_size");
    LOG_F(2, "param blob size : {}", info.params_size);

    // Consume reserved tail bytes for the fixed-width ProceduralMeshInfo arm.
    constexpr size_t kProceduralConsumed = sizeof(uint32_t);
    constexpr size_t kProceduralTail
      = sizeof(data::pak::geometry::ProceduralMeshInfo) - kProceduralConsumed;
    static_assert(kProceduralTail == 68);
    auto skip_result
      = reader.Forward(static_cast<std::streamoff>(kProceduralTail));
    CheckResult(skip_result, "m.procedural.reserved");

    std::vector<std::byte> param_blob;
    if (info.params_size > 0) {
      param_blob.resize(info.params_size);
      auto param_blob_result = reader.ReadBlobInto(std::span(param_blob));
      CheckResult(param_blob_result, "m.param_blob");
    }

    // Use GenerateMeshBuffers to get vertices/indices
    auto mesh_opt
      = data::GenerateMeshBuffers(name, std::span<const std::byte>(param_blob));
    if (!mesh_opt || mesh_opt->first.empty() || mesh_opt->second.empty()) {
      throw std::runtime_error(fmt::format(
        "Failed to generate procedural mesh '{}': unsupported generator or "
        "invalid parameters",
        name));
    }
    std::tie(vertices, indices) = std::move(*mesh_opt);
  }

  inline auto LoadSkinnedMeshBuffers(
    LoaderContext& context, data::pak::geometry::MeshDesc& desc)
    -> std::pair<std::shared_ptr<data::BufferResource>,
      std::shared_ptr<data::BufferResource>>
  {
    using data::BufferResource;
    using data::ResourceReferenceIndex;

    DCHECK_NOTNULL_F(
      context.desc_reader, "expecting desc_reader not to be null");
    auto& reader = *context.desc_reader;

    auto& info = desc.info.skinned;

    auto vb_result
      = reader.ReadInto<ResourceReferenceIndex>(info.vertex_buffer);
    CheckResult(vb_result, "m.vertex_buffer");
    LOG_F(2, "vertex buffer   : {}", info.vertex_buffer);

    auto ib_result = reader.ReadInto<ResourceReferenceIndex>(info.index_buffer);
    CheckResult(ib_result, "m.index_buffer");
    LOG_F(2, "index buffer    : {}", info.index_buffer);

    auto joint_index_result
      = reader.ReadInto<ResourceReferenceIndex>(info.joint_index_buffer);
    CheckResult(joint_index_result, "m.joint_index_buffer");
    LOG_F(2, "joint index buf : {}", info.joint_index_buffer);

    auto joint_weight_result
      = reader.ReadInto<ResourceReferenceIndex>(info.joint_weight_buffer);
    CheckResult(joint_weight_result, "m.joint_weight_buffer");
    LOG_F(2, "joint weight buf: {}", info.joint_weight_buffer);

    auto inverse_bind_result
      = reader.ReadInto<ResourceReferenceIndex>(info.inverse_bind_buffer);
    CheckResult(inverse_bind_result, "m.inverse_bind_buffer");
    LOG_F(2, "inverse bind buf: {}", info.inverse_bind_buffer);

    auto joint_remap_result
      = reader.ReadInto<ResourceReferenceIndex>(info.joint_remap_buffer);
    CheckResult(joint_remap_result, "m.joint_remap_buffer");
    LOG_F(2, "joint remap buf : {}", info.joint_remap_buffer);

    auto skeleton_key_result
      = reader.ReadInto<data::AssetKey>(info.skeleton_asset_key);
    CheckResult(skeleton_key_result, "m.skeleton_asset_key");
    LOG_F(2, "skeleton asset  : {}", info.skeleton_asset_key);

    auto joint_count_result = reader.ReadInto<uint16_t>(info.joint_count);
    CheckResult(joint_count_result, "m.joint_count");
    LOG_F(2, "joint count     : {}", info.joint_count);

    auto influences_result
      = reader.ReadInto<uint16_t>(info.influences_per_vertex);
    CheckResult(influences_result, "m.influences_per_vertex");
    LOG_F(2, "influences/vtx  : {}", info.influences_per_vertex);

    auto flags_result = reader.ReadInto<uint32_t>(info.flags);
    CheckResult(flags_result, "m.flags");
    LOG_F(2, "skinning flags  : {}", info.flags);

    for (float& i : info.bounding_box_min) {
      auto min_result = reader.ReadInto<float>(i);
      CheckResult(min_result, "m.bounding_box_min");
    }
    for (float& i : info.bounding_box_max) {
      auto max_result = reader.ReadInto<float>(i);
      CheckResult(max_result, "m.bounding_box_max");
    }

    if (context.parse_only) {
      return { nullptr, nullptr };
    }

    if (!context.dependency_collector) {
      LOG_F(ERROR,
        "GeometryLoader requires a DependencyCollector for non-parse-only "
        "loads (skinned mesh buffers)");
      throw std::runtime_error(
        "GeometryLoader requires a DependencyCollector for async decode");
    }

    auto collect_buffer_ref
      = [&](const ResourceReferenceIndex reference) -> void {
      const auto resource_index
        = context.ResolveResource(reference, data::ResourceKind::kBuffer);
      if (!resource_index) {
        return;
      }
      internal::ResourceRef ref {
        .source = context.source_instance,
        .resource_type_id = BufferResource::ClassTypeId(),
        .resource_index = *resource_index,
      };
      context.dependency_collector->AddResourceDependency(ref);
    };

    collect_buffer_ref(info.vertex_buffer);
    collect_buffer_ref(info.index_buffer);
    collect_buffer_ref(info.joint_index_buffer);
    collect_buffer_ref(info.joint_weight_buffer);
    collect_buffer_ref(info.inverse_bind_buffer);
    collect_buffer_ref(info.joint_remap_buffer);

    return { nullptr, nullptr };
  }

  inline auto LoadMeshViewDesc(serio::AnyReader& desc_reader)
    -> data::pak::geometry::MeshViewDesc
  {
    LOG_SCOPE_F(1, "Mesh View");

    using oxygen::data::pak::geometry::MeshViewDesc;

    // Read MeshViewDesc from the stream
    MeshViewDesc mesh_view_desc;
    auto mesh_view_result = desc_reader.Read<MeshViewDesc>();
    CheckResult(mesh_view_result, "m.desc");
    mesh_view_desc = *mesh_view_result;
    LOG_F(2, "first vertex  : {}", mesh_view_desc.first_vertex);
    LOG_F(2, "vertex count  : {}", mesh_view_desc.vertex_count);
    LOG_F(2, "first index   : {}", mesh_view_desc.first_index);
    LOG_F(2, "index count   : {}", mesh_view_desc.index_count);
    LOG_F(2, "bounding box  : ({}, {}, {}) - ({}, {}, {})",
      mesh_view_desc.bounding_box_min[0], mesh_view_desc.bounding_box_min[1],
      mesh_view_desc.bounding_box_min[2], mesh_view_desc.bounding_box_max[0],
      mesh_view_desc.bounding_box_max[1], mesh_view_desc.bounding_box_max[2]);

    return mesh_view_desc;
  }

  //! Rejects view bounds that are inverted or leave the submesh bounds.
  inline auto ValidateMeshViewBounds(
    const data::pak::geometry::SubMeshDesc& submesh,
    const data::pak::geometry::MeshViewDesc& view, const uint32_t submesh_index,
    const std::size_t view_index) -> void
  {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto min = view.bounding_box_min[axis];
      const auto max = view.bounding_box_max[axis];
      if (!(min <= max) || !(min >= submesh.bounding_box_min[axis])
        || !(max <= submesh.bounding_box_max[axis])) {
        throw std::runtime_error(fmt::format(
          "submesh {} mesh view {} bounds are outside the submesh bounds",
          submesh_index, view_index));
      }
    }
  }

  inline auto LoadSubMeshDesc(serio::AnyReader& desc_reader)
    -> data::pak::geometry::SubMeshDesc
  {
    LOG_SCOPE_F(1, "Sub-Mesh");

    using oxygen::data::AssetKey;
    using oxygen::data::pak::geometry::SubMeshDesc;

    SubMeshDesc desc;

    // name_offset
    auto submesh_name_result = desc_reader.ReadInto(desc.name_offset);
    CheckResult(submesh_name_result, "sm.name_offset");
    LOG_F(2, "name offset    : {}", desc.name_offset);

    // material_asset_key
    auto mat_key_result
      = desc_reader.ReadInto<AssetKey>(desc.material_asset_key);
    CheckResult(mat_key_result, "sm.material_asset_key");
    LOG_F(2, "material asset : {}", desc.material_asset_key);

    data::SurfaceId::ByteArray surface_bytes {};
    CheckResult(desc_reader.ReadBlobInto(
                  std::as_writable_bytes(std::span(surface_bytes))),
      "sm.surface_id");
    desc.surface_id = data::SurfaceId::FromBytes(surface_bytes);
    if (desc.surface_id.IsNil()) {
      throw std::runtime_error("Geometry surface identity is nil");
    }

    // mesh_view_count
    auto mesh_view_count_result
      = desc_reader.ReadInto<uint32_t>(desc.mesh_view_count);
    CheckResult(mesh_view_count_result, "sm.mesh_view_count");
    LOG_F(2, "mesh view count: {}", desc.mesh_view_count);

    // bounding_box_min
    ReadBoundingBox(desc_reader, desc.bounding_box_min, "sm.bounding_box_min");
    LOG_F(2, "bounding box min: ({}, {}, {})", desc.bounding_box_min[0],
      desc.bounding_box_min[1], desc.bounding_box_min[2]);

    // bounding_box_max
    ReadBoundingBox(desc_reader, desc.bounding_box_max, "sm.bounding_box_max");
    LOG_F(2, "bounding box max: ({}, {}, {})", desc.bounding_box_max[0],
      desc.bounding_box_max[1], desc.bounding_box_max[2]);

    return desc;
  }

  inline auto LoadSubMeshViews(serio::AnyReader& reader,
    uint32_t mesh_view_count) -> std::vector<data::pak::geometry::MeshViewDesc>
  {
    using oxygen::data::pak::geometry::MeshViewDesc;

    std::vector<MeshViewDesc> mesh_views;
    mesh_views.reserve(mesh_view_count);
    for (uint32_t i = 0; i < mesh_view_count; ++i) {
      auto mesh_view_desc = LoadMeshViewDesc(reader);
      mesh_views.push_back(mesh_view_desc);
    }
    return mesh_views;
  }

} // namespace detail

inline auto LoadMesh(LoaderContext context, const data::LodIndex lod,
  const data::DescriptorStrings& strings) -> std::unique_ptr<data::Mesh>
{
  LOG_SCOPE_F(1, "Mesh");
  LOG_F(2, "offline mode    : {}", context.work_offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  using oxygen::data::AssetKey;
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::MeshType;
  using oxygen::data::Vertex;
  using oxygen::data::pak::geometry::MeshDesc;

  // Read MeshDesc fields one by one
  MeshDesc desc;
  auto name_result = reader.ReadInto(desc.name_offset);
  detail::CheckResult(name_result, "m.name_offset");
  const auto name
    = ResolveDescriptorString(strings, desc.name_offset, "mesh name");
  LOG_F(2, "name            : {}", name);

  // mesh_type (must be read before union)
  auto mesh_type_result = reader.ReadInto<uint8_t>(desc.mesh_type);
  detail::CheckResult(mesh_type_result, "m.mesh_type");
  LOG_F(2, "mesh type       : {}", static_cast<MeshType>(desc.mesh_type));

  // submesh_count
  auto submesh_count_result = reader.ReadInto<uint32_t>(desc.submesh_count);
  detail::CheckResult(submesh_count_result, "m.submesh_count");
  LOG_F(2, "submesh count   : {}", desc.submesh_count);

  // mesh_view_count
  auto mesh_view_count_result = reader.ReadInto<uint32_t>(desc.mesh_view_count);
  detail::CheckResult(mesh_view_count_result, "m.mesh_view_count");
  LOG_F(2, "mesh view count : {}", desc.mesh_view_count);

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::shared_ptr<data::BufferResource> vertex_buffer_resource;
  std::shared_ptr<data::BufferResource> index_buffer_resource;

  if (desc.IsStandard()) {
    // For standard meshes, load buffer resources (no data copying)
    std::tie(vertex_buffer_resource, index_buffer_resource)
      = detail::LoadStandardMeshBuffers(context, desc);
  } else if (desc.IsSkinned()) {
    std::tie(vertex_buffer_resource, index_buffer_resource)
      = detail::LoadSkinnedMeshBuffers(context, desc);
  } else if (desc.IsProcedural()) {
    // For procedural meshes, generate vertex/index data (owned data)
    detail::LoadProceduralMeshBuffers(reader, desc, name, vertices, indices);
  } else {
    throw std::runtime_error(fmt::format(
      "unsupported mesh type: {}", static_cast<int>(desc.mesh_type)));
  }

  MeshBuilder builder(lod, name);
  builder.WithDescriptor(desc);

  if (desc.IsProcedural()) {
    builder.WithVertices(std::move(vertices)).WithIndices(std::move(indices));
  } else {
    builder.WithBufferResources(vertex_buffer_resource, index_buffer_resource);
  }

  uint32_t total_read_views { 0 };
  for (uint32_t i = 0; i < desc.submesh_count; ++i) {
    auto sm_desc = detail::LoadSubMeshDesc(reader);
    auto mesh_views = detail::LoadSubMeshViews(reader, sm_desc.mesh_view_count);
    if (sm_desc.mesh_view_count != mesh_views.size()) {
      throw std::runtime_error(
        fmt::format("submesh {} has {} mesh views, expected {}", i,
          mesh_views.size(), sm_desc.mesh_view_count));
    }
    for (std::size_t view = 0; view < mesh_views.size(); ++view) {
      detail::ValidateMeshViewBounds(sm_desc, mesh_views[view], i, view);
    }
    total_read_views += sm_desc.mesh_view_count;

    auto sm_name
      = ResolveDescriptorString(strings, sm_desc.name_offset, "submesh name");

    // Resolve the material asset key to a MaterialAsset
    std::shared_ptr<const MaterialAsset> material;
    if (context.dependency_collector
      && sm_desc.material_asset_key != AssetKey {}) {
      context.dependency_collector->AddAssetDependency(
        sm_desc.material_asset_key);
    }
    if (context.parse_only) {
      material = MaterialAsset::CreateDefault();
    } else if (context.dependency_collector) {
      material = MaterialAsset::CreateDefault();
    } else {
      LOG_F(ERROR,
        "GeometryLoader requires a DependencyCollector for non-parse-only "
        "loads (material dependencies)");
      throw std::runtime_error(
        "GeometryLoader requires a DependencyCollector for async decode");
    }

    auto sm_builder
      = builder.BeginSubMesh(sm_name, material).WithDescriptor(sm_desc);
    for (const auto& mv_desc : mesh_views) {
      sm_builder.WithMeshView(mv_desc);
    }
    builder.EndSubMesh(std::move(sm_builder));
  }

  if (total_read_views != desc.mesh_view_count) {
    throw std::runtime_error(
      fmt::format("total read mesh views ({}) != expected ({})",
        total_read_views, desc.mesh_view_count));
  }

  return builder.Build();
}

inline auto LoadGeometryAsset(LoaderContext context)
  -> std::unique_ptr<data::GeometryAsset>
{
  using oxygen::data::Mesh;
  using oxygen::data::pak::geometry::GeometryAssetDesc;

  LOG_SCOPE_F(1, "Geometry");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  // Read GeometryAssetDesc field by field
  GeometryAssetDesc desc;

  auto pack = reader.ScopedAlignment(1);
  const auto descriptor_start = reader.Position();
  detail::CheckResult(descriptor_start, "g.position");

  // header (use header loader from Helpers.h)
  LoadAssetHeader(reader, desc.header);

  if (desc.header.asset_type != static_cast<uint8_t>(data::AssetType::kGeometry)
    || desc.header.version != data::pak::geometry::kGeometryAssetVersion) {
    throw std::runtime_error("Geometry version requires recooking");
  }
  const auto strings
    = ReadDescriptorStrings(reader, *descriptor_start, desc.header);

  // lod_count
  auto lod_count_result = reader.ReadInto<uint32_t>(desc.lod_count);
  detail::CheckResult(lod_count_result, "g.lod_count");
  LOG_F(2, "LOD count      : {}", desc.lod_count);
  LOG_F(2, "offline mode   : {}", context.work_offline ? "yes" : "no");

  // bounding_box_min
  detail::ReadBoundingBox(reader, desc.bounding_box_min, "g.bounding_box_min");

  // bounding_box_max
  detail::ReadBoundingBox(reader, desc.bounding_box_max, "g.bounding_box_max");

  if (desc.lod_count == 0
    || desc.lod_count > data::pak::geometry::kMaxLodCount) {
    throw std::runtime_error(
      fmt::format("geometry has {} LODs, expected 1 to {}", desc.lod_count,
        data::pak::geometry::kMaxLodCount));
  }

  auto surface_count_result = reader.ReadInto<uint32_t>(desc.surface_count);
  detail::CheckResult(surface_count_result, "g.surface_count");
  LOG_F(2, "surface count  : {}", desc.surface_count);

  auto lod_policy_result = reader.Read<data::pak::geometry::LodPolicyRecord>();
  detail::CheckResult(lod_policy_result, "g.default_lod_policy");
  desc.default_lod_policy = *lod_policy_result;

  // Read LOD meshes
  std::vector<std::shared_ptr<Mesh>> lod_meshes;
  for (uint32_t i = 0; i < desc.lod_count; ++i) {
    auto mesh = LoadMesh(context, data::LodIndex { i }, strings);
    lod_meshes.push_back(std::move(mesh));
  }

  // A cooked geometry declares the defaults of every surface it declares, and
  // every surface has at least one submesh.
  auto submesh_count = std::size_t { 0 };
  for (const auto& mesh : lod_meshes) {
    submesh_count += mesh->SubMeshes().size();
  }
  if (desc.surface_count > submesh_count) {
    throw std::runtime_error(
      fmt::format("geometry has {} surface defaults for {} submeshes",
        desc.surface_count, submesh_count));
  }
  std::vector<data::pak::geometry::SurfaceDesc> surface_table;
  surface_table.reserve(desc.surface_count);
  for (uint32_t i = 0; i < desc.surface_count; ++i) {
    auto surface_result = reader.Read<data::pak::geometry::SurfaceDesc>();
    detail::CheckResult(surface_result, "s.surface");
    surface_table.push_back(*surface_result);
  }
  SkipDescriptorStrings(reader, *descriptor_start, desc.header);

  if (context.asset_references) {
    std::vector<data::ResourceReferenceUse> resources;
    std::vector<data::KeyReference> keys;
    const auto buffer
      = [&resources](const data::ResourceReferenceIndex index) -> void {
      resources.push_back({
        .reference = index,
        .kind = data::ResourceKind::kBuffer,
      });
    };
    for (const auto& mesh : lod_meshes) {
      bool needs_vertices = false;
      bool needs_indices = false;
      for (const auto& submesh : mesh->SubMeshes()) {
        for (const auto& view : submesh.MeshViews()) {
          needs_indices = needs_indices || view.IndexCount() != 0U;
          needs_vertices
            = needs_vertices || view.VertexCount() != 0U || needs_indices;
        }
      }
      const auto require_buffers
        = [needs_vertices, needs_indices](
            const data::ResourceReferenceIndex vertex,
            const data::ResourceReferenceIndex index) -> void {
        if ((needs_vertices && vertex == data::kNoResourceReference)
          || (needs_indices && index == data::kNoResourceReference)) {
          throw std::runtime_error(
            "Geometry draw requires an absent buffer binding");
        }
      };
      const auto& descriptor = mesh->Descriptor();
      if (descriptor.has_value() && descriptor->IsStandard()) {
        require_buffers(descriptor->info.standard.vertex_buffer,
          descriptor->info.standard.index_buffer);
        buffer(descriptor->info.standard.vertex_buffer);
        buffer(descriptor->info.standard.index_buffer);
      } else if (const auto* skin = mesh->SkinnedDescriptor()) {
        require_buffers(skin->vertex_buffer, skin->index_buffer);
        buffer(skin->vertex_buffer);
        buffer(skin->index_buffer);
        buffer(skin->joint_index_buffer);
        buffer(skin->joint_weight_buffer);
        buffer(skin->inverse_bind_buffer);
        buffer(skin->joint_remap_buffer);
        keys.push_back({
          .key = skin->skeleton_asset_key,
          .kind = data::KeyReferenceKind::kLogical,
          .expected_type = data::AssetType::kUnknown,
        });
      }
      for (const auto& submesh : mesh->SubMeshes()) {
        if (const auto& sub = submesh.Descriptor(); sub.has_value()) {
          keys.push_back({
            .key = sub->material_asset_key,
            .kind = data::KeyReferenceKind::kAsset,
            .expected_type = data::AssetType::kMaterial,
          });
        }
      }
    }
    context.ValidateReferences(resources, keys);
  }

  // Construct and return GeometryAsset with LOD meshes; it rejects a surface
  // table that does not list exactly the declared surfaces, and an invalid
  // default LOD policy.
  const auto name_offset = desc.header.name_offset;
  auto geometry = std::make_unique<data::GeometryAsset>(
    context.current_asset_key, std::move(desc), std::move(lod_meshes),
    data::SourceOrigin { context.source_key, context.source_instance },
    surface_table);
  geometry->SetAssetName(
    ResolveDescriptorString(strings, name_offset, "asset name"));
  return geometry;
}

static_assert(oxygen::content::LoadFunction<decltype(LoadGeometryAsset)>);

} // namespace oxygen::content::loaders
