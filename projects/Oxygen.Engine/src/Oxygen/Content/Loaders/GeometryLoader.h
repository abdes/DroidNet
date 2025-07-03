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
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::loaders {

namespace detail {
  // Accepts any Result<T> and checks for error, logs and throws if needed
  template <typename ResultT>
  void CheckResult(const ResultT& result, const char* field_name)
  {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field_name, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading geometry asset ({}): {}", field_name,
          result.error().message()));
    }
  }

  // Helper to read a bounding box (float[3])
  template <oxygen::serio::Stream S>
  void ReadBoundingBox(
    oxygen::serio::Reader<S>& reader, float (&bbox)[3], const char* field_name)
  {
    for (int i = 0; i < 3; ++i) {
      auto f_result = reader.template read<float>();
      CheckResult(f_result, field_name);
      bbox[i] = *f_result;
    }
  }

  template <oxygen::serio::Stream S>
  auto LoadMeshViewDesc(oxygen::serio::Reader<S> reader)
    -> data::pak::MeshViewDesc
  {
    LOG_SCOPE_F(INFO, "Mesh View");

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    // Read MeshViewDesc from the stream
    auto mesh_view_result = reader.template read<MeshViewDesc>();
    detail::CheckResult(mesh_view_result, "m.desc");
    const auto& mesh_view_desc = *mesh_view_result;
    LOG_F(INFO, "firs vertex   : {}", mesh_view_desc.first_vertex);
    LOG_F(INFO, "vertex count  : {}", mesh_view_desc.vertex_count);
    LOG_F(INFO, "first index   : {}", mesh_view_desc.first_index);
    LOG_F(INFO, "index count   : {}", mesh_view_desc.index_count);

    return *mesh_view_result;
  }

  template <oxygen::serio::Stream S>
  auto LoadSubMeshDesc(oxygen::serio::Reader<S> reader)
    -> data::pak::SubMeshDesc
  {
    LOG_SCOPE_F(INFO, "Sub-Mesh");

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    SubMeshDesc desc;
    // name
    std::span<std::byte> submesh_name_span(
      reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
    auto submesh_name_result = reader.read_blob_to(submesh_name_span);
    detail::CheckResult(submesh_name_result, "sm.name");
    LOG_F(INFO, "name           : {}", desc.name);

    // material_asset_key
    auto mat_key_result = reader.template read<AssetKey>();
    detail::CheckResult(mat_key_result, "sm.material_asset_key");
    desc.material_asset_key = *mat_key_result;
    LOG_F(
      INFO, "material asset : {}", nostd::to_string(desc.material_asset_key));

    // mesh_view_count
    auto mesh_view_count_result = reader.template read<uint32_t>();
    detail::CheckResult(mesh_view_count_result, "sm.mesh_view_count");
    desc.mesh_view_count = *mesh_view_count_result;
    LOG_F(INFO, "mesh view count: {}", desc.mesh_view_count);

    // bounding_box_min
    ReadBoundingBox(reader, desc.bounding_box_min, "sm.bounding_box_min");
    LOG_F(INFO, "bounding box min: ({}, {}, {})", desc.bounding_box_min[0],
      desc.bounding_box_min[1], desc.bounding_box_min[2]);

    // bounding_box_max
    ReadBoundingBox(reader, desc.bounding_box_max, "sm.bounding_box_max");
    LOG_F(INFO, "bounding box max: ({}, {}, {})", desc.bounding_box_max[0],
      desc.bounding_box_max[1], desc.bounding_box_max[2]);

    return desc;
  }

  template <oxygen::serio::Stream S>
  auto LoadSubMeshViews(oxygen::serio::Reader<S> reader,
    uint32_t mesh_view_count) -> std::vector<data::pak::MeshViewDesc>
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

template <oxygen::serio::Stream S>
auto LoadMesh(oxygen::serio::Reader<S> reader) -> std::shared_ptr<data::Mesh>
{
  LOG_SCOPE_F(INFO, "Mesh");

  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Read MeshDesc fields one by one
  MeshDesc desc;
  std::span<std::byte> name_span(
    reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
  auto name_result = reader.read_blob_to(name_span);
  detail::CheckResult(name_result, "m.name");
  LOG_F(INFO, "name            : {}", desc.name);

  // vertex_buffer
  auto vb_result = reader.template read<ResourceIndexT>();
  detail::CheckResult(vb_result, "m.vertex_buffer");
  desc.vertex_buffer = *vb_result;
  LOG_F(INFO, "vertex buffer   : {}", desc.vertex_buffer);

  // index_buffer
  auto ib_result = reader.template read<ResourceIndexT>();
  detail::CheckResult(ib_result, "m.index_buffer");
  desc.index_buffer = *ib_result;
  LOG_F(INFO, "index buffer    : {}", desc.index_buffer);

  // submesh_count
  auto submesh_count_result = reader.template read<uint32_t>();
  detail::CheckResult(submesh_count_result, "m.submesh_count");
  desc.submesh_count = *submesh_count_result;
  LOG_F(INFO, "submesh count   : {}", desc.submesh_count);

  // mesh_view_count
  auto mesh_view_count_result = reader.template read<uint32_t>();
  detail::CheckResult(mesh_view_count_result, "m.mesh_view_count");
  desc.mesh_view_count = *mesh_view_count_result;
  LOG_F(INFO, "mesh view count : {}", desc.mesh_view_count);

  // bounding_box_min
  detail::ReadBoundingBox(reader, desc.bounding_box_min, "m.bounding_box_min");
  LOG_F(INFO, "bounding box min: ({}, {}, {})", desc.bounding_box_min[0],
    desc.bounding_box_min[1], desc.bounding_box_min[2]);
  // bounding_box_max
  detail::ReadBoundingBox(reader, desc.bounding_box_max, "m.bounding_box_max");
  LOG_F(INFO, "bounding box max: ({}, {}, {})", desc.bounding_box_max[0],
    desc.bounding_box_max[1], desc.bounding_box_max[2]);

  // Placeholder: actual vertex/index buffer loading not implemented
  std::vector<Vertex> vertices { 200 }; // TODO: load from stream
  std::vector<std::uint32_t> indices { 200 }; // TODO: load from stream

  std::string name(
    desc.name, std::find(desc.name, desc.name + kMaxNameSize, '\0'));

  MeshBuilder builder(/*lod=*/0, name);
  builder.WithVertices(vertices).WithIndices(indices);

  for (uint32_t i = 0; i < desc.submesh_count; ++i) {
    auto sm_desc = detail::LoadSubMeshDesc(reader);
    auto mesh_views = detail::LoadSubMeshViews(reader, sm_desc.mesh_view_count);

    std::string sm_name;
    if (auto nul
      = std::find(std::begin(sm_desc.name), std::end(sm_desc.name), '\0');
      nul != std::end(sm_desc.name)) {
      sm_name.assign(sm_desc.name, nul);
    } else {
      sm_name.assign(sm_desc.name, sm_desc.name + kMaxNameSize);
    }

    // Placeholder: material asset is not loaded here
    std::shared_ptr<const MaterialAsset> material;

    auto sm_builder = builder.BeginSubMesh(sm_name, material);
    for (const auto& mv_desc : mesh_views) {
      sm_builder.WithMeshView(mv_desc);
    }
    builder.EndSubMesh(std::move(sm_builder));
  }

  // Build and return the mesh
  return builder.Build();
}

template <oxygen::serio::Stream S>
auto LoadGeometryAsset(oxygen::serio::Reader<S> reader)
  -> std::unique_ptr<data::GeometryAsset>
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  LOG_SCOPE_F(INFO, "Geometry");

  // Read GeometryAssetDesc field by field
  GeometryAssetDesc desc;

  auto pack = reader.ScopedAlignement(1);

  // header (use header loader from Helpers.h)
  desc.header = LoadAssetHeader(reader);

  // lod_count
  auto lod_count_result = reader.template read<uint32_t>();
  detail::CheckResult(lod_count_result, "g.lod_count");
  desc.lod_count = *lod_count_result;
  DLOG_F(INFO, "GeometryAsset LOD count: {}", desc.lod_count);

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
    auto mesh = LoadMesh(reader);
    lod_meshes.push_back(std::move(mesh));
  }

  // Construct and return GeometryAsset with LOD meshes
  return std::make_unique<GeometryAsset>(
    std::move(desc), std::move(lod_meshes));
}

} // namespace oxygen::content::loaders
