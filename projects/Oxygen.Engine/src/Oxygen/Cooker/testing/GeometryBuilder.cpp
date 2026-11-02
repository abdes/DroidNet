//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/ProductLoad.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::cooker::testing {

namespace {

  namespace geometry = data::pak::geometry;

  //! The default defaults of a surface.
  constexpr auto kVisibleAndCastsShadows
    = geometry::kSurfaceFlag_Visible | geometry::kSurfaceFlag_CastsShadows;

  //! Writes `bounds` into the bounds of `record`.
  template <typename Record>
  auto SetBounds(Record& record, const auto& bounds) -> void
  {
    const auto& [min, max] = bounds;
    std::ranges::copy(
      std::array { min.x, min.y, min.z }, std::begin(record.bounding_box_min));
    std::ranges::copy(
      std::array { max.x, max.y, max.z }, std::begin(record.bounding_box_max));
  }

} // namespace

GeometryBuilder::GeometryBuilder(const std::string_view name)
  : name_(name)
{
}

auto GeometryBuilder::ProceduralLod(std::string mesh_name) -> uint32_t
{
  const auto generated = data::GenerateMeshBuffers(mesh_name, {});
  if (!generated.has_value() || generated->first.empty()) {
    throw std::invalid_argument(
      "the product generates no procedural mesh '" + mesh_name + "'");
  }
  const auto& [vertices, indices] = *generated;
  auto lod = Lod {
    .type = data::MeshType::kProcedural,
    .mesh_name = std::move(mesh_name),
    .buffers = std::nullopt,
    .bounds = {
      .min = vertices.front().position,
      .max = vertices.front().position,
    },
    .whole = {
      .first_index = 0U,
      .index_count = static_cast<uint32_t>(indices.size()),
      .first_vertex = 0U,
      .vertex_count = static_cast<uint32_t>(vertices.size()),
    },
    .submeshes = {},
  };
  for (const auto& vertex : vertices) {
    lod.bounds.min = glm::min(lod.bounds.min, vertex.position);
    lod.bounds.max = glm::max(lod.bounds.max, vertex.position);
  }
  lods_.push_back(std::move(lod));
  return static_cast<uint32_t>(lods_.size() - 1U);
}

auto GeometryBuilder::StandardLod(const ResourceIndexT vertices,
  const ResourceIndexT indices, const glm::vec3& min, const glm::vec3& max,
  std::string mesh_name) -> uint32_t
{
  lods_.push_back({
    .type = data::MeshType::kStandard,
    .mesh_name = std::move(mesh_name),
    .buffers = std::pair { vertices, indices },
    .bounds = { .min = min, .max = max },
    .whole = {},
    .submeshes = {},
  });
  return static_cast<uint32_t>(lods_.size() - 1U);
}

auto GeometryBuilder::Submesh(const uint32_t lod,
  const data::SurfaceId& surface, const data::AssetKey& material,
  std::vector<geometry::MeshViewDesc> views, std::string name)
  -> GeometryBuilder&
{
  if (lod >= lods_.size()) {
    throw std::invalid_argument("the geometry '" + name_ + "' has no LOD "
      + std::to_string(lod) + " for a submesh");
  }
  auto& target = lods_.at(lod);
  const auto procedural = target.type == data::MeshType::kProcedural;
  if (procedural != views.empty()) {
    throw std::invalid_argument("a submesh of the geometry '" + name_
      + "' draws the whole procedural mesh, or views of a standard one");
  }
  if (procedural) {
    views.push_back(target.whole);
  }
  auto record = geometry::SubMeshDesc {};
  record.surface_id = surface;
  record.material_asset_key = material;
  record.mesh_view_count = static_cast<uint32_t>(views.size());
  target.submeshes.push_back({
    .record = record,
    .name = std::move(name),
    .views = std::move(views),
  });
  return *this;
}

auto GeometryBuilder::SurfaceFlags(
  const data::SurfaceId& surface, const uint32_t flags) -> GeometryBuilder&
{
  surface_flags_.emplace_back(surface, flags);
  return *this;
}

auto GeometryBuilder::DefaultLodPolicy(const geometry::LodPolicyRecord policy)
  -> GeometryBuilder&
{
  policy_ = policy;
  return *this;
}

auto GeometryBuilder::Lods() const -> std::vector<Lod>
{
  if (!lods_.empty()) {
    return lods_;
  }
  auto cube = GeometryBuilder { name_ };
  cube.Submesh(cube.ProceduralLod(),
    data::SurfaceId::FromStableIdentity("fixture-surface"));
  return cube.lods_;
}

auto GeometryBuilder::Bytes() const -> std::vector<std::byte>
{
  const auto lods = Lods();
  auto descriptor = geometry::GeometryAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kGeometry);
  descriptor.header.version = geometry::kGeometryAssetVersion;
  descriptor.lod_count = static_cast<uint32_t>(lods.size());
  descriptor.default_lod_policy = policy_;
  auto strings = data::DescriptorStringsBuilder {};
  descriptor.header.name_offset = strings.Add(name_);
  auto references = content::import::AssetReferenceBuilder {};
  auto surfaces = std::vector<geometry::SurfaceDesc> {};
  auto records = std::vector<std::byte> {};
  auto bounds = lods.front().bounds;
  for (const auto& lod : lods) {
    bounds.min = glm::min(bounds.min, lod.bounds.min);
    bounds.max = glm::max(bounds.max, lod.bounds.max);
    auto mesh = geometry::MeshDesc {};
    mesh.name_offset = strings.Add(lod.mesh_name);
    mesh.mesh_type = static_cast<uint8_t>(lod.type);
    if (lod.buffers.has_value()) {
      mesh.info.standard.vertex_buffer = references.AddResource(
        data::ResourceKind::kBuffer, lod.buffers->first);
      mesh.info.standard.index_buffer = references.AddResource(
        data::ResourceKind::kBuffer, lod.buffers->second);
      SetBounds(mesh.info.standard, lod.bounds);
    } else {
      mesh.info.procedural = {};
    }
    mesh.submesh_count = static_cast<uint32_t>(lod.submeshes.size());
    for (const auto& submesh : lod.submeshes) {
      mesh.mesh_view_count += static_cast<uint32_t>(submesh.views.size());
    }
    AppendRecord(records, mesh);
    for (const auto& submesh : lod.submeshes) {
      auto record = submesh.record;
      record.name_offset = strings.Add(submesh.name);
      SetBounds(record, lod.bounds);
      references.AddAsset(
        record.material_asset_key, data::AssetType::kMaterial);
      AppendRecord(records, record);
      for (auto view : submesh.views) {
        SetBounds(view, lod.bounds);
        AppendRecord(records, view);
      }
      const auto listed = std::ranges::any_of(
        surfaces, [&record](const geometry::SurfaceDesc& surface) -> bool {
          return surface.surface_id == record.surface_id;
        });
      if (!listed) {
        const auto set = std::ranges::find_if(
          surface_flags_, [&record](const auto& entry) -> bool {
            return entry.first == record.surface_id;
          });
        surfaces.push_back({
          .surface_id = record.surface_id,
          .flags
          = set == surface_flags_.end() ? kVisibleAndCastsShadows : set->second,
        });
      }
    }
  }
  SetBounds(descriptor, bounds);
  descriptor.surface_count = static_cast<uint32_t>(surfaces.size());
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, descriptor);
  bytes.insert(bytes.end(), records.begin(), records.end());
  for (const auto& surface : surfaces) {
    AppendRecord(bytes, surface);
  }
  data::AppendDescriptorStrings(bytes, strings);
  data::StampDescriptorHash(bytes);
  const auto encoded_references = std::move(references).Build();
  detail::CheckWithLoader("geometry '" + name_ + "'", bytes, encoded_references,
    content::loaders::LoadGeometryAsset);
  return bytes;
}

auto GeometryBuilder::References() const -> data::AssetReferences
{
  auto references = content::import::AssetReferenceBuilder {};
  for (const auto& lod : Lods()) {
    if (lod.buffers.has_value()) {
      static_cast<void>(references.AddResource(
        data::ResourceKind::kBuffer, lod.buffers->first));
      static_cast<void>(references.AddResource(
        data::ResourceKind::kBuffer, lod.buffers->second));
    }
    for (const auto& submesh : lod.submeshes) {
      references.AddAsset(
        submesh.record.material_asset_key, data::AssetType::kMaterial);
    }
  }
  return std::move(references).Build();
}

} // namespace oxygen::cooker::testing
