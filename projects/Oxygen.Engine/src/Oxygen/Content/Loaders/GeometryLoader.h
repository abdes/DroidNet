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
    LOG_SCOPE_FUNCTION(INFO);

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    // Read MeshViewDesc from the stream
    auto mesh_view_result = reader.template read<MeshViewDesc>();
    detail::CheckResult(mesh_view_result, "m.desc");
    return *mesh_view_result;
  }

  template <oxygen::serio::Stream S>
  auto LoadSubMeshDesc(oxygen::serio::Reader<S> reader)
    -> data::pak::SubMeshDesc
  {
    LOG_SCOPE_FUNCTION(INFO);

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    SubMeshDesc desc;
    // name
    std::span<std::byte> submesh_name_span(
      reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
    auto submesh_name_result = reader.read_blob_to(submesh_name_span);
    detail::CheckResult(submesh_name_result, "sm.name");

    // material_asset_key
    auto mat_key_result = reader.template read<AssetKey>();
    detail::CheckResult(mat_key_result, "sm.material_asset_key");
    desc.material_asset_key = *mat_key_result;

    // mesh_view_count
    auto mesh_view_count_result = reader.template read<uint32_t>();
    detail::CheckResult(mesh_view_count_result, "sm.mesh_view_count");
    desc.mesh_view_count = *mesh_view_count_result;

    // bounding_box_min
    ReadBoundingBox(reader, desc.bounding_box_min, "sm.bounding_box_min");

    // bounding_box_max
    ReadBoundingBox(reader, desc.bounding_box_max, "sm.bounding_box_max");

    return desc;
  }

  template <oxygen::serio::Stream S>
  auto LoadSubMeshViews(oxygen::serio::Reader<S> reader,
    uint32_t mesh_view_count) -> std::vector<data::pak::MeshViewDesc>
  {
    LOG_SCOPE_FUNCTION(INFO);

    using namespace oxygen::data;
    using namespace oxygen::data::pak;

    std::vector<MeshViewDesc> mesh_views;
    mesh_views.reserve(mesh_view_count);
    for (uint32_t i = 0; i < mesh_view_count; ++i) {
      auto mesh_view_result = reader.template read<MeshViewDesc>();
      detail::CheckResult(mesh_view_result, "mv.desc");
      mesh_views.push_back(*mesh_view_result);
    }
    return mesh_views;
  }

} // namespace detail

template <oxygen::serio::Stream S>
auto LoadMesh(oxygen::serio::Reader<S> reader) -> std::shared_ptr<data::Mesh>
{
  LOG_SCOPE_FUNCTION(INFO);

  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Read MeshDesc fields one by one
  MeshDesc desc;
  std::span<std::byte> name_span(
    reinterpret_cast<std::byte*>(desc.name), kMaxNameSize);
  auto name_result = reader.read_blob_to(name_span);
  detail::CheckResult(name_result, "m.name");

  // vertex_buffer
  auto vb_result = reader.template read<ResourceIndexT>();
  detail::CheckResult(vb_result, "m.vertex_buffer");
  desc.vertex_buffer = *vb_result;

  // index_buffer
  auto ib_result = reader.template read<ResourceIndexT>();
  detail::CheckResult(ib_result, "m.index_buffer");
  desc.index_buffer = *ib_result;

  // submesh_count
  auto submesh_count_result = reader.template read<uint32_t>();
  detail::CheckResult(submesh_count_result, "m.submesh_count");
  desc.submesh_count = *submesh_count_result;

  // mesh_view_count
  auto mesh_view_count_result = reader.template read<uint32_t>();
  detail::CheckResult(mesh_view_count_result, "m.mesh_view_count");
  desc.mesh_view_count = *mesh_view_count_result;

  // bounding_box_min
  detail::ReadBoundingBox(reader, desc.bounding_box_min, "m.bounding_box_min");
  // bounding_box_max
  detail::ReadBoundingBox(reader, desc.bounding_box_max, "m.bounding_box_max");

  // Placeholder: actual vertex/index buffer loading not implemented
  std::vector<Vertex> vertices; // TODO: load from stream
  std::vector<std::uint32_t> indices; // TODO: load from stream

  std::string name(
    desc.name, std::find(desc.name, desc.name + kMaxNameSize, '\0'));

  MeshBuilder builder(/*lod=*/0, name);
  builder.WithVertices(vertices).WithIndices(indices);

  for (uint32_t i = 0; i < desc.submesh_count; ++i) {
    auto desc = detail::LoadSubMeshDesc(reader);
    auto mesh_views = detail::LoadSubMeshViews(reader, desc.mesh_view_count);

    std::string sm_name;
    if (auto nul = std::find(std::begin(desc.name), std::end(desc.name), '\0');
      nul != std::end(desc.name)) {
      sm_name.assign(desc.name, nul);
    } else {
      sm_name.assign(desc.name, desc.name + kMaxNameSize);
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
  LOG_SCOPE_FUNCTION(INFO);
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Read GeometryAssetDesc field by field
  GeometryAssetDesc desc;

  // header (use header loader from Helpers.h)
  desc.header = LoadAssetHeader(reader);

  // lod_count
  auto lod_count_result = reader.template read<uint32_t>();
  detail::CheckResult(lod_count_result, "g.lod_count");
  desc.lod_count = *lod_count_result;

  // bounding_box_min
  detail::ReadBoundingBox(reader, desc.bounding_box_min, "g.bounding_box_min");

  // bounding_box_max
  detail::ReadBoundingBox(reader, desc.bounding_box_max, "g.bounding_box_max");

  // reserved: skip forward instead of reading
  constexpr std::streamoff reserved_size = sizeof(desc.reserved);
  auto skip_result = reader.forward(reserved_size);
  detail::CheckResult(skip_result, "g.reserved (skip)");

  // Construct and return GeometryAsset
  return std::make_unique<GeometryAsset>(std::move(desc));
}

} // namespace oxygen::content::loaders
