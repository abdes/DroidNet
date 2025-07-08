//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>

namespace oxygen::content::loaders {

namespace detail {

  // Accepts any Result<T> and checks for error, logs and throws if needed
  template <typename ResultT>
  auto CheckResult(const ResultT& result, const char* field_name) -> void
  {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field_name, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading geometry asset ({}): {}", field_name,
          result.error().message()));
    }
  }

  // Helper to read a bounding box (float[3])
  template <serio::Stream S>
  auto ReadBoundingBox(
    serio::Reader<S>& reader, float (&bbox)[3], const char* field_name) -> void
  {
    for (int i = 0; i < 3; ++i) {
      auto f_result = reader.template read<float>();
      CheckResult(f_result, field_name);
      bbox[i] = *f_result;
    }
  }

  // Handles loading of vertex/index buffers for standard meshes
  template <serio::Stream DescS, serio::Stream DataS>
  auto LoadStandardMeshBuffers(LoaderContext<DescS, DataS>& context,
    data::pak::MeshDesc& desc, std::vector<data::Vertex>& vertices,
    std::vector<uint32_t>& indices) -> void
  {
    using namespace oxygen::data::pak;

    serio::Reader<DescS>& reader = context.desc_reader.get();

    // Access union field via desc.info.standard
    auto& info = desc.info.standard;
    // Read vertex_buffer
    auto vb_result = reader.template read<ResourceIndexT>();
    CheckResult(vb_result, "m.vertex_buffer");
    info.vertex_buffer = *vb_result;
    LOG_F(2, "vertex buffer   : {}", info.vertex_buffer);
    if (info.vertex_buffer != 0 && context.asset_loader) {
      LOG_F(2, "Registering resource dependency: vertex_buffer = {}",
        info.vertex_buffer);
      context.asset_loader->AddResourceDependency(
        context.current_asset_key, info.vertex_buffer);
    }

    // Read index_buffer
    auto ib_result = reader.template read<ResourceIndexT>();
    CheckResult(ib_result, "m.index_buffer");
    info.index_buffer = *ib_result;
    LOG_F(2, "index buffer    : {}", info.index_buffer);
    if (info.index_buffer != 0 && context.asset_loader) {
      LOG_F(2, "Registering resource dependency: index_buffer = {}",
        info.index_buffer);
      context.asset_loader->AddResourceDependency(
        context.current_asset_key, info.index_buffer);
    }

    for (float& i : desc.info.standard.bounding_box_min) {
      auto min_result = reader.template read<float>();
      CheckResult(min_result, "m.bounding_box_min");
      i = *min_result;
    }
    for (float& i : desc.info.standard.bounding_box_max) {
      auto max_result = reader.template read<float>();
      CheckResult(max_result, "m.bounding_box_max");
      i = *max_result;
    }

    // TODO: Read the vertex and index buffer data and attach to mesh
    // For now we just do some sanity checks and throw if the data cannot be
    // fully read

    const auto& buf_table
      = context.source_pak->template GetResourceTable<data::BufferResource>();
    if (!buf_table) {
      LOG_F(ERROR, "no resource table for buffers");
      throw std::runtime_error("no resource table for buffers");
    }

    // Save current position to restore it later, as we are going to jump around
    auto pos_result = reader.position();
    CheckResult(pos_result, "Save desc reader position");
    {
      auto vb_offset = buf_table->GetResourceOffset(info.vertex_buffer);
      if (!vb_offset) {
        LOG_F(ERROR, "-failed- on vertex buffer not found: index = {}",
          info.vertex_buffer);
        throw std::runtime_error("Vertex buffer not found");
      }
      CheckResult(reader.seek(*vb_offset), "Vertex Buffer Data");
      auto buf_resource = LoadBufferResource(context);
    }
    {
      auto ib_offset = buf_table->GetResourceOffset(info.index_buffer);
      if (!ib_offset) {
        LOG_F(ERROR, "-failed- on index buffer not found: index = {}",
          info.index_buffer);
        throw std::runtime_error("Vertex buffer not found");
      }
      CheckResult(reader.seek(*ib_offset), "Index Buffer Data");
      auto buf_resource = LoadBufferResource(context);
    }
    // Restore reader position after reading buffers
    CheckResult(
      reader.seek(pos_result.value()), "Restore desc reader position");
  }

  // Handles loading and generation for procedural meshes
  template <serio::Stream S>
  auto LoadProceduralMeshBuffers(serio::Reader<S>& reader,
    data::pak::MeshDesc& desc, std::vector<data::Vertex>& vertices,
    std::vector<uint32_t>& indices) -> void
  {
    // Access union field via desc.info.procedural
    auto& info = desc.info.procedural;

    // Read params_size from the stream
    auto params_size_result = reader.template read<uint32_t>();
    CheckResult(params_size_result, "m.param_blob_size");
    info.params_size = *params_size_result;
    LOG_F(2, "param blob size : {}", info.params_size);

    std::vector<std::byte> param_blob;
    if (info.params_size > 0) {
      param_blob.resize(info.params_size);
      auto param_blob_result
        = reader.read_blob_to(std::span<std::byte>(param_blob));
      CheckResult(param_blob_result, "m.param_blob");
    }

    // Use GenerateMeshBuffers to get vertices/indices
    auto mesh_opt = data::GenerateMeshBuffers(
      desc.name, std::span<const std::byte>(param_blob));
    if (mesh_opt) {
      std::tie(vertices, indices) = std::move(*mesh_opt);
    } else {
      LOG_F(ERROR, "Failed to generate procedural mesh for {}", desc.name);
    }
  }

  template <serio::Stream S>
  auto LoadMeshViewDesc(serio::Reader<S> desc_reader) -> data::pak::MeshViewDesc
  {
    LOG_SCOPE_F(INFO, "Mesh View");

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    // Read MeshViewDesc from the stream
    auto mesh_view_result = desc_reader.template read<MeshViewDesc>();
    detail::CheckResult(mesh_view_result, "m.desc");
    const auto& mesh_view_desc = *mesh_view_result;
    LOG_F(INFO, "firs vertex   : {}", mesh_view_desc.first_vertex);
    LOG_F(INFO, "vertex count  : {}", mesh_view_desc.vertex_count);
    LOG_F(INFO, "first index   : {}", mesh_view_desc.first_index);
    LOG_F(INFO, "index count   : {}", mesh_view_desc.index_count);

    return *mesh_view_result;
  }

  template <serio::Stream S>
  auto LoadSubMeshDesc(serio::Reader<S> desc_reader) -> data::pak::SubMeshDesc
  {
    LOG_SCOPE_F(INFO, "Sub-Mesh");

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    SubMeshDesc desc;
    // name
    std::span<std::byte> submesh_name_span(
      reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
    auto submesh_name_result = desc_reader.read_blob_to(submesh_name_span);
    detail::CheckResult(submesh_name_result, "sm.name");
    LOG_F(2, "name           : {}", desc.name);

    // material_asset_key
    auto mat_key_result = desc_reader.template read<AssetKey>();
    detail::CheckResult(mat_key_result, "sm.material_asset_key");
    desc.material_asset_key = *mat_key_result;
    LOG_F(2, "material asset : {}", nostd::to_string(desc.material_asset_key));

    // mesh_view_count
    auto mesh_view_count_result = desc_reader.template read<uint32_t>();
    detail::CheckResult(mesh_view_count_result, "sm.mesh_view_count");
    desc.mesh_view_count = *mesh_view_count_result;
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

  template <serio::Stream S>
  auto LoadSubMeshViews(serio::Reader<S> reader, uint32_t mesh_view_count)
    -> std::vector<data::pak::MeshViewDesc>
  {
    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    std::vector<MeshViewDesc> mesh_views;
    mesh_views.reserve(mesh_view_count);
    for (uint32_t i = 0; i < mesh_view_count; ++i) {
      auto mesh_view_desc = detail::LoadMeshViewDesc(reader);
      mesh_views.push_back(mesh_view_desc);
    }
    return mesh_views;
  }

} // namespace detail

template <serio::Stream DescS, serio::Stream DataS>
auto LoadMesh(LoaderContext<DescS, DataS> context)
  -> std::unique_ptr<data::Mesh>
{
  LOG_SCOPE_F(INFO, "Mesh");
  LOG_F(2, "offline mode    : {}", context.offline ? "yes" : "no");

  auto& reader = context.desc_reader.get();

  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Read MeshDesc fields one by one
  MeshDesc desc;
  std::span<std::byte> name_span(
    reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
  auto name_result = reader.read_blob_to(name_span);
  detail::CheckResult(name_result, "m.name");
  LOG_F(2, "name            : {}", desc.name);
  std::cerr << "[DEBUG] MeshDesc.name offset: "
            << reader.position().value() - kMaxNameSize << std::endl;

  // mesh_type (must be read before union)
  auto mesh_type_result = reader.template read<uint8_t>();
  detail::CheckResult(mesh_type_result, "m.mesh_type");
  desc.mesh_type = *mesh_type_result;
  LOG_F(2, "mesh type       : {}",
    nostd::to_string(static_cast<MeshType>(desc.mesh_type)));
  std::cerr << "[DEBUG] MeshDesc.mesh_type offset: "
            << reader.position().value() - 1 << std::endl;

  // submesh_count
  auto submesh_count_result = reader.template read<uint32_t>();
  detail::CheckResult(submesh_count_result, "m.submesh_count");
  desc.submesh_count = *submesh_count_result;
  LOG_F(2, "submesh count   : {}", desc.submesh_count);
  std::cerr << "[DEBUG] MeshDesc.submesh_count offset: "
            << reader.position().value() - 4 << std::endl;

  // mesh_view_count
  auto mesh_view_count_result = reader.template read<uint32_t>();
  detail::CheckResult(mesh_view_count_result, "m.mesh_view_count");
  desc.mesh_view_count = *mesh_view_count_result;
  LOG_F(2, "mesh view count : {}", desc.mesh_view_count);
  std::cerr << "[DEBUG] MeshDesc.mesh_view_count offset: "
            << reader.position().value() - 4 << std::endl;

  std::cerr << "[DEBUG] MeshDesc union offset: " << reader.position().value()
            << std::endl;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  if (desc.IsStandard()) {
    detail::LoadStandardMeshBuffers(context, desc, vertices, indices);
    std::cerr << "[DEBUG] MeshDesc union end offset: "
              << reader.position().value() << std::endl;
  } else if (desc.IsProcedural()) {
    detail::LoadProceduralMeshBuffers(reader, desc, vertices, indices);
    std::cerr << "[DEBUG] MeshDesc union end offset: "
              << reader.position().value() << std::endl;
  } else {
    LOG_F(ERROR, "Unsupported mesh type: {}", static_cast<int>(desc.mesh_type));
    return nullptr;
  }

  std::string name(
    desc.name, std::find(desc.name, desc.name + kMaxNameSize, '\0'));

  MeshBuilder builder(/*lod=*/0, name);
  builder.WithDescriptor(desc).WithVertices(vertices).WithIndices(indices);

  uint32_t total_read_views { 0 };
  for (uint32_t i = 0; i < desc.submesh_count; ++i) {
    std::cerr << "[DEBUG] SubMeshDesc start offset: "
              << reader.position().value() << std::endl;
    auto sm_desc = detail::LoadSubMeshDesc(reader);

    context.asset_loader->AddAssetDependency(
      context.current_asset_key, sm_desc.material_asset_key);

    auto mesh_views = detail::LoadSubMeshViews(reader, sm_desc.mesh_view_count);
    std::cerr << "[DEBUG] After MeshViewDesc offset: "
              << reader.position().value() << std::endl;
    if (sm_desc.mesh_view_count != mesh_views.size()) {
      LOG_F(ERROR, "SubMesh {} has {} mesh views, expected {}", i,
        mesh_views.size(), sm_desc.mesh_view_count);
      return nullptr;
    }
    total_read_views += sm_desc.mesh_view_count;

    std::string sm_name;
    if (auto nul
      = std::find(std::begin(sm_desc.name), std::end(sm_desc.name), '\0');
      nul != std::end(sm_desc.name)) {
      sm_name.assign(sm_desc.name, nul);
    } else {
      sm_name.assign(sm_desc.name, sm_desc.name + kMaxNameSize);
    }

    // TODO: Resolve the material asset key to a MaterialAsset
    std::shared_ptr<const MaterialAsset> material
      = MaterialAsset::CreateDefault();

    auto sm_builder
      = builder.BeginSubMesh(sm_name, material).WithDescriptor(sm_desc);
    for (const auto& mv_desc : mesh_views) {
      sm_builder.WithMeshView(mv_desc);
    }
    builder.EndSubMesh(std::move(sm_builder));
  }

  if (total_read_views != desc.mesh_view_count) {
    LOG_F(ERROR, "Total read mesh views ({}) != expected ({})",
      total_read_views, desc.mesh_view_count);
    return nullptr;
  }

  return builder.Build();
}

template <serio::Stream DescS, serio::Stream DataS>
auto LoadGeometryAsset(LoaderContext<DescS, DataS> context)
  -> std::unique_ptr<data::GeometryAsset>
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  LOG_SCOPE_F(INFO, "Geometry");

  auto& reader = context.desc_reader.get();

  // Read GeometryAssetDesc field by field
  GeometryAssetDesc desc;

  auto pack = reader.ScopedAlignment(1);

  // header (use header loader from Helpers.h)
  desc.header = LoadAssetHeader(reader);

  // lod_count
  auto lod_count_result = reader.template read<uint32_t>();
  detail::CheckResult(lod_count_result, "g.lod_count");
  desc.lod_count = *lod_count_result;
  LOG_F(2, "LOD count      : {}", desc.lod_count);
  LOG_F(2, "offline mode   : {}", context.offline ? "yes" : "no");

  // bounding_box_min
  detail::ReadBoundingBox(reader, desc.bounding_box_min, "g.bounding_box_min");

  // bounding_box_max
  detail::ReadBoundingBox(reader, desc.bounding_box_max, "g.bounding_box_max");

  // reserved: skip forward instead of reading
  constexpr std::streamoff reserved_size = sizeof(desc.reserved);
  auto skip_result = reader.forward(reserved_size);
  detail::CheckResult(skip_result, "g.reserved (skip)");

  // Read LOD meshes
  std::vector<std::shared_ptr<Mesh>> lod_meshes;
  for (uint32_t i = 0; i < desc.lod_count; ++i) {
    auto mesh = LoadMesh(context);
    lod_meshes.push_back(std::move(mesh));
  }

  // Dependencies are registered inline during mesh loading

  // Construct and return GeometryAsset with LOD meshes
  return std::make_unique<GeometryAsset>(
    std::move(desc), std::move(lod_meshes));
}

//! Unload function for GeometryAsset.
inline auto UnloadGeometryAsset(std::shared_ptr<data::GeometryAsset> /*asset*/,
  AssetLoader& /*loader*/, bool /*offline*/) noexcept -> void
{
  // Nothing to do for a geometry asset, its dependency resources will do the
  // work when unloaded.
}

} // namespace oxygen::content::loaders
