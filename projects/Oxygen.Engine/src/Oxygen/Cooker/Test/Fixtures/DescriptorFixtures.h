//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>

namespace oxygen::content::test {

struct DescriptorFixture final {
  std::vector<std::byte> bytes;
  data::AssetReferences references;
};

enum class MaterialVariant : uint8_t { kPlain, kWithShader };

template <typename T>
auto AppendRecord(std::vector<std::byte>& bytes, const T& record) -> void
{
  const auto encoded = std::as_bytes(std::span(&record, 1U));
  bytes.insert(bytes.end(), encoded.begin(), encoded.end());
}

inline auto SetName(const std::string_view name, const std::span<char> field)
  -> void
{
  std::ranges::fill(field, '\0');
  std::ranges::copy(name.substr(0U, field.size() - 1U), field.begin());
}

inline auto MaterialDescriptor(const std::string_view name,
  const MaterialVariant variant = MaterialVariant::kPlain) -> DescriptorFixture
{
  data::pak::render::MaterialAssetDesc descriptor {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kMaterial);
  descriptor.header.version = data::pak::render::kMaterialAssetVersion;
  SetName(name, descriptor.header.name);
  if (variant == MaterialVariant::kWithShader) {
    descriptor.shader_stages = 1U << static_cast<uint32_t>(ShaderType::kPixel);
  }
  DescriptorFixture fixture;
  AppendRecord(fixture.bytes, descriptor);
  if (variant == MaterialVariant::kWithShader) {
    data::pak::render::ShaderReferenceDesc shader {};
    shader.shader_type = static_cast<uint8_t>(ShaderType::kPixel);
    SetName("fixture.hlsl", shader.source_path);
    SetName("PS", shader.entry_point);
    AppendRecord(fixture.bytes, shader);
  }
  return fixture;
}

inline auto TexturedMaterialDescriptor(const std::string_view name,
  const ResourceIndexT target,
  const MaterialVariant variant = MaterialVariant::kPlain) -> DescriptorFixture
{
  auto fixture = MaterialDescriptor(name, variant);
  data::pak::render::MaterialAssetDesc descriptor {};
  std::memcpy(&descriptor, fixture.bytes.data(), sizeof(descriptor));
  descriptor.base_color_texture = data::ResourceReferenceIndex { 0U };
  std::memcpy(fixture.bytes.data(), &descriptor, sizeof(descriptor));
  fixture.references = data::AssetReferences::Create(
    {
      {
        .kind = data::ResourceKind::kTexture,
        .index = target,
      },
    },
    {})
                         .value();
  return fixture;
}

inline auto GeometryDescriptor(const std::string_view name) -> DescriptorFixture
{
  data::pak::geometry::GeometryAssetDesc descriptor {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kGeometry);
  descriptor.header.version = data::pak::geometry::kGeometryAssetVersion;
  descriptor.lod_count = 1U;
  SetName(name, descriptor.header.name);
  data::pak::geometry::MeshDesc mesh {};
  SetName("Cube/Mesh", mesh.name);
  mesh.mesh_type = static_cast<uint8_t>(data::MeshType::kProcedural);
  mesh.info.procedural = {};
  mesh.submesh_count = 1U;
  mesh.mesh_view_count = 1U;
  data::pak::geometry::SubMeshDesc submesh {};
  submesh.slot_id = data::MaterialSlotId::FromStableIdentity("fixture-slot");
  submesh.mesh_view_count = 1U;
  constexpr data::pak::geometry::MeshViewDesc view {
    .first_index = 0U,
    .index_count = 36U,
    .first_vertex = 0U,
    .vertex_count = 24U,
  };
  DescriptorFixture fixture;
  AppendRecord(fixture.bytes, descriptor);
  AppendRecord(fixture.bytes, mesh);
  AppendRecord(fixture.bytes, submesh);
  AppendRecord(fixture.bytes, view);
  return fixture;
}

inline auto SceneDescriptor(const std::string_view name,
  const uint32_t string_bytes = 1U) -> DescriptorFixture
{
  if (string_bytes == 0U) {
    throw std::invalid_argument("Scene fixture needs a NUL string table entry");
  }
  data::pak::world::SceneAssetDesc descriptor {};
  descriptor.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  descriptor.header.version = data::pak::world::kSceneAssetVersion;
  SetName(name, descriptor.header.name);
  descriptor.scene_strings = {
    .offset = sizeof(descriptor),
    .size = string_bytes,
  };
  data::pak::world::SceneEnvironmentBlockHeader environment {};
  environment.byte_size = sizeof(environment);
  DescriptorFixture fixture;
  AppendRecord(fixture.bytes, descriptor);
  fixture.bytes.resize(fixture.bytes.size() + string_bytes, std::byte { 'x' });
  fixture.bytes.at(sizeof(descriptor)) = std::byte {};
  fixture.bytes.back() = std::byte {};
  AppendRecord(fixture.bytes, environment);
  return fixture;
}

} // namespace oxygen::content::test
