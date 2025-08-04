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
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Content/Loaders/Helpers.h>

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
    LoaderContext& context, data::pak::MeshDesc& desc)
    -> std::pair<std::shared_ptr<data::BufferResource>,
      std::shared_ptr<data::BufferResource>>
  {
    using namespace oxygen::data::pak;

    DCHECK_NOTNULL_F(
      context.desc_reader, "expecting desc_reader not to be null");
    auto& reader = *context.desc_reader;

    // Access union field via desc.info.standard
    auto& info = desc.info.standard;

    // Read vertex_buffer first (according to StandardMeshInfo layout)
    auto vb_result = reader.ReadInto<ResourceIndexT>(info.vertex_buffer);
    CheckResult(vb_result, "m.vertex_buffer");
    LOG_F(2, "vertex buffer   : {}", info.vertex_buffer);

    // Read index_buffer second
    auto ib_result = reader.ReadInto<ResourceIndexT>(info.index_buffer);
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

    // Load vertex and index buffer data using AssetLoader with robust error
    // handling

    std::shared_ptr<data::BufferResource> vertex_buffer_resource;
    std::shared_ptr<data::BufferResource> index_buffer_resource;

    auto vb_resource_key
      = context.asset_loader->MakeResourceKey<data::BufferResource>(
        *context.source_pak, info.vertex_buffer);

    auto ib_resource_key
      = context.asset_loader->MakeResourceKey<data::BufferResource>(
        *context.source_pak, info.index_buffer);

    std::optional<ResourceCleanupGuard> vertex_guard;
    std::optional<ResourceCleanupGuard> index_guard;

    // Load vertex buffer resource if specified
    if (info.vertex_buffer != 0) {
      vertex_buffer_resource
        = context.asset_loader->LoadResource<data::BufferResource>(
          *context.source_pak, info.vertex_buffer, context.offline);
      if (!vertex_buffer_resource) {
        LOG_F(ERROR, "-failed- to load vertex buffer resource: index = {}",
          info.vertex_buffer);
        throw std::runtime_error("Failed to load vertex buffer resource");
      }
      LOG_F(2, "Loaded vertex buffer resource: {} bytes",
        vertex_buffer_resource->GetData().size());

      // Create cleanup guard - will auto-release if an exception occurs
      vertex_guard.emplace(*context.asset_loader, vb_resource_key);
    }

    // Load index buffer resource if specified
    if (info.index_buffer != 0) {
      index_buffer_resource
        = context.asset_loader->LoadResource<data::BufferResource>(
          *context.source_pak, info.index_buffer, context.offline);
      if (!index_buffer_resource) {
        LOG_F(ERROR, "-failed- to load index buffer resource: index = {}",
          info.index_buffer);
        throw std::runtime_error("Failed to load index buffer resource");
      }
      LOG_F(2, "Loaded index buffer resource: {} bytes",
        index_buffer_resource->GetData().size());

      // Create cleanup guard - will auto-release if an exception occurs
      index_guard.emplace(*context.asset_loader, ib_resource_key);
    }

    // Register dependencies only after all resources are successfully loaded
    if (info.vertex_buffer != 0) {
      LOG_F(2, "Registering resource dependency: vertex_buffer = {}",
        info.vertex_buffer);
      context.asset_loader->AddResourceDependency(
        context.current_asset_key, vb_resource_key);
      vertex_guard->disable(); // Success - disable cleanup
    }

    if (info.index_buffer != 0) {
      LOG_F(2, "Registering resource dependency: index_buffer = {}",
        info.index_buffer);
      context.asset_loader->AddResourceDependency(
        context.current_asset_key, ib_resource_key);
      index_guard->disable(); // Success - disable cleanup
    }

    return { vertex_buffer_resource, index_buffer_resource };
  }

  // Handles loading and generation for procedural meshes
  inline auto LoadProceduralMeshBuffers(serio::AnyReader& reader,
    data::pak::MeshDesc& desc, std::vector<data::Vertex>& vertices,
    std::vector<uint32_t>& indices) -> void
  {
    // Access union field via desc.info.procedural
    auto& info = desc.info.procedural;

    // Read params_size from the stream
    auto params_size_result = reader.ReadInto<uint32_t>(info.params_size);
    CheckResult(params_size_result, "m.param_blob_size");
    LOG_F(2, "param blob size : {}", info.params_size);

    std::vector<std::byte> param_blob;
    if (info.params_size > 0) {
      param_blob.resize(info.params_size);
      auto param_blob_result = reader.ReadBlobInto(std::span(param_blob));
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

  inline auto LoadMeshViewDesc(serio::AnyReader& desc_reader)
    -> data::pak::MeshViewDesc
  {
    LOG_SCOPE_F(INFO, "Mesh View");

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    // Read MeshViewDesc from the stream
    MeshViewDesc mesh_view_desc;
    auto mesh_view_result = desc_reader.Read<MeshViewDesc>();
    CheckResult(mesh_view_result, "m.desc");
    mesh_view_desc = *mesh_view_result;
    LOG_F(INFO, "firs vertex   : {}", mesh_view_desc.first_vertex);
    LOG_F(INFO, "vertex count  : {}", mesh_view_desc.vertex_count);
    LOG_F(INFO, "first index   : {}", mesh_view_desc.first_index);
    LOG_F(INFO, "index count   : {}", mesh_view_desc.index_count);

    return mesh_view_desc;
  }

  inline auto LoadSubMeshDesc(serio::AnyReader& desc_reader)
    -> data::pak::SubMeshDesc
  {
    LOG_SCOPE_F(INFO, "Sub-Mesh");

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    SubMeshDesc desc;
    // name
    std::span submesh_name_span(
      reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
    auto submesh_name_result = desc_reader.ReadBlobInto(submesh_name_span);
    CheckResult(submesh_name_result, "sm.name");
    LOG_F(2, "name           : {}", desc.name);

    // material_asset_key
    auto mat_key_result
      = desc_reader.ReadInto<AssetKey>(desc.material_asset_key);
    CheckResult(mat_key_result, "sm.material_asset_key");
    LOG_F(2, "material asset : {}", nostd::to_string(desc.material_asset_key));

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

  auto LoadSubMeshViews(serio::AnyReader& reader, uint32_t mesh_view_count)
    -> std::vector<data::pak::MeshViewDesc>
  {
    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    std::vector<MeshViewDesc> mesh_views;
    mesh_views.reserve(mesh_view_count);
    for (uint32_t i = 0; i < mesh_view_count; ++i) {
      auto mesh_view_desc = LoadMeshViewDesc(reader);
      mesh_views.push_back(mesh_view_desc);
    }
    return mesh_views;
  }

} // namespace detail

inline auto LoadMesh(LoaderContext context) -> std::unique_ptr<data::Mesh>
{
  LOG_SCOPE_F(INFO, "Mesh");
  LOG_F(2, "offline mode    : {}", context.offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Read MeshDesc fields one by one
  MeshDesc desc;
  std::span name_span(reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
  auto name_result = reader.ReadBlobInto(name_span);
  detail::CheckResult(name_result, "m.name");
  LOG_F(2, "name            : {}", desc.name);
  std::cerr << "[DEBUG] MeshDesc.name offset: "
            << reader.Position().value() - kMaxNameSize << std::endl;

  // mesh_type (must be read before union)
  auto mesh_type_result = reader.ReadInto<uint8_t>(desc.mesh_type);
  detail::CheckResult(mesh_type_result, "m.mesh_type");
  LOG_F(2, "mesh type       : {}",
    nostd::to_string(static_cast<MeshType>(desc.mesh_type)));
  std::cerr << "[DEBUG] MeshDesc.mesh_type offset: "
            << reader.Position().value() - 1 << std::endl;

  // submesh_count
  auto submesh_count_result = reader.ReadInto<uint32_t>(desc.submesh_count);
  detail::CheckResult(submesh_count_result, "m.submesh_count");
  LOG_F(2, "submesh count   : {}", desc.submesh_count);
  std::cerr << "[DEBUG] MeshDesc.submesh_count offset: "
            << reader.Position().value() - 4 << std::endl;

  // mesh_view_count
  auto mesh_view_count_result = reader.ReadInto<uint32_t>(desc.mesh_view_count);
  detail::CheckResult(mesh_view_count_result, "m.mesh_view_count");
  LOG_F(2, "mesh view count : {}", desc.mesh_view_count);
  std::cerr << "[DEBUG] MeshDesc.mesh_view_count offset: "
            << reader.Position().value() - 4 << std::endl;

  std::cerr << "[DEBUG] MeshDesc union offset: " << reader.Position().value()
            << std::endl;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::shared_ptr<data::BufferResource> vertex_buffer_resource;
  std::shared_ptr<data::BufferResource> index_buffer_resource;

  if (desc.IsStandard()) {
    // For standard meshes, load buffer resources (no data copying)
    std::tie(vertex_buffer_resource, index_buffer_resource)
      = detail::LoadStandardMeshBuffers(context, desc);
    std::cerr << "[DEBUG] MeshDesc union end offset: "
              << reader.Position().value() << std::endl;
  } else if (desc.IsProcedural()) {
    // For procedural meshes, generate vertex/index data (owned data)
    detail::LoadProceduralMeshBuffers(reader, desc, vertices, indices);
    std::cerr << "[DEBUG] MeshDesc union end offset: "
              << reader.Position().value() << std::endl;
  } else {
    LOG_F(ERROR, "Unsupported mesh type: {}", static_cast<int>(desc.mesh_type));
    return nullptr;
  }

  std::string name(
    desc.name, std::find(desc.name, desc.name + kMaxNameSize, '\0'));

  MeshBuilder builder(/*lod=*/0, name);
  builder.WithDescriptor(desc);

  // Configure builder based on mesh type
  if (desc.IsStandard()) {
    // Reference external buffer resources (zero-copy)
    builder.WithBufferResources(vertex_buffer_resource, index_buffer_resource);
  } else if (desc.IsProcedural()) {
    // Use owned vertex/index data (data is moved into builder)
    builder.WithVertices(vertices).WithIndices(indices);
  }

  uint32_t total_read_views { 0 };
  for (uint32_t i = 0; i < desc.submesh_count; ++i) {
    std::cerr << "[DEBUG] SubMeshDesc start offset: "
              << reader.Position().value() << std::endl;
    auto sm_desc = detail::LoadSubMeshDesc(reader);

    auto mesh_views = detail::LoadSubMeshViews(reader, sm_desc.mesh_view_count);
    std::cerr << "[DEBUG] After MeshViewDesc offset: "
              << reader.Position().value() << std::endl;
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

    // Resolve the material asset key to a MaterialAsset
    std::shared_ptr<const MaterialAsset> material;
    if (sm_desc.material_asset_key != AssetKey {}) {
      // Attempt to load the actual material asset referenced by the submesh
      material = context.asset_loader->LoadAsset<MaterialAsset>(
        sm_desc.material_asset_key, context.offline);

      if (material) {
        context.asset_loader->AddAssetDependency(
          context.current_asset_key, sm_desc.material_asset_key);
        LOG_F(2,
          "Successfully loaded material and registered dependency on material "
          "asset: {}",
          nostd::to_string(sm_desc.material_asset_key));
      } else {
        LOG_F(WARNING, "Failed to load material asset {}, using default",
          nostd::to_string(sm_desc.material_asset_key));
        material = MaterialAsset::CreateDefault();
      }
    } else {
      // Use default material if no material asset key is specified
      LOG_F(2, "No material asset key specified, using default material");
      material = MaterialAsset::CreateDefault();
    }

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

auto LoadGeometryAsset(LoaderContext context)
  -> std::unique_ptr<data::GeometryAsset>
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  LOG_SCOPE_F(INFO, "Geometry");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  // Read GeometryAssetDesc field by field
  GeometryAssetDesc desc;

  auto pack = reader.ScopedAlignment(1);

  // header (use header loader from Helpers.h)
  LoadAssetHeader(reader, desc.header);

  // lod_count
  auto lod_count_result = reader.ReadInto<uint32_t>(desc.lod_count);
  detail::CheckResult(lod_count_result, "g.lod_count");
  LOG_F(2, "LOD count      : {}", desc.lod_count);
  LOG_F(2, "offline mode   : {}", context.offline ? "yes" : "no");

  // bounding_box_min
  detail::ReadBoundingBox(reader, desc.bounding_box_min, "g.bounding_box_min");

  // bounding_box_max
  detail::ReadBoundingBox(reader, desc.bounding_box_max, "g.bounding_box_max");

  // reserved: skip forward instead of reading
  constexpr std::streamoff reserved_size = sizeof(desc.reserved);
  auto skip_result = reader.Forward(reserved_size);
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

static_assert(oxygen::content::LoadFunction<decltype(LoadGeometryAsset)>);

//! Unload function for GeometryAsset.
inline auto UnloadGeometryAsset(std::shared_ptr<data::GeometryAsset> /*asset*/,
  AssetLoader& /*loader*/, bool /*offline*/) noexcept -> void
{
  // Nothing to do for a geometry asset, its dependency resources will do the
  // work when unloaded.
}

static_assert(oxygen::content::UnloadFunction<decltype(UnloadGeometryAsset),
  data::GeometryAsset>);

} // namespace oxygen::content::loaders
