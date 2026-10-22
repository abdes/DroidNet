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
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SurfaceId.h>

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

inline auto MaterialDescriptor(const std::string_view name,
  const MaterialVariant variant = MaterialVariant::kPlain) -> DescriptorFixture
{
  data::pak::render::MaterialAssetDesc descriptor {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kMaterial);
  descriptor.header.version = data::pak::render::kMaterialAssetVersion;
  auto strings = data::DescriptorStringsBuilder {};
  descriptor.header.name_offset = strings.Add(name);
  if (variant == MaterialVariant::kWithShader) {
    descriptor.shader_stages = 1U << static_cast<uint32_t>(ShaderType::kPixel);
  }
  DescriptorFixture fixture;
  AppendRecord(fixture.bytes, descriptor);
  if (variant == MaterialVariant::kWithShader) {
    data::pak::render::ShaderReferenceDesc shader {};
    shader.shader_type = static_cast<uint8_t>(ShaderType::kPixel);
    shader.source_path_offset = strings.Add("fixture.hlsl");
    shader.entry_point_offset = strings.Add("PS");
    AppendRecord(fixture.bytes, shader);
  }
  data::AppendDescriptorStrings(fixture.bytes, strings);
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
  auto strings = data::DescriptorStringsBuilder {};
  descriptor.header.name_offset = strings.Add(name);
  data::pak::geometry::MeshDesc mesh {};
  mesh.name_offset = strings.Add("Cube/Mesh");
  mesh.mesh_type = static_cast<uint8_t>(data::MeshType::kProcedural);
  mesh.info.procedural = {};
  mesh.submesh_count = 1U;
  mesh.mesh_view_count = 1U;
  data::pak::geometry::SubMeshDesc submesh {};
  submesh.surface_id = data::SurfaceId::FromStableIdentity("fixture-surface");
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
  data::AppendDescriptorStrings(fixture.bytes, strings);
  return fixture;
}

//! A scene with no nodes; `padding` grows its string table, for size tests.
inline auto SceneDescriptor(
  const std::string_view name, const uint32_t padding = 0U) -> DescriptorFixture
{
  data::pak::world::SceneAssetDesc descriptor {};
  descriptor.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  descriptor.header.version = data::pak::world::kSceneAssetVersion;
  auto strings = data::DescriptorStringsBuilder {};
  descriptor.header.name_offset = strings.Add(name);
  if (padding != 0U) {
    static_cast<void>(strings.Add(std::string(padding, 'x')));
  }
  descriptor.header.strings = {
    .offset = sizeof(descriptor),
    .size = strings.Size(),
  };
  data::pak::world::SceneEnvironmentBlockHeader environment {};
  environment.byte_size = sizeof(environment);
  DescriptorFixture fixture;
  AppendRecord(fixture.bytes, descriptor);
  const auto table = strings.Bytes();
  fixture.bytes.insert(fixture.bytes.end(), table.begin(), table.end());
  AppendRecord(fixture.bytes, environment);
  return fixture;
}

//! Resolves a string of the cooked descriptor `bytes` hold.
/*!
 @throw std::invalid_argument when the table or the offset is invalid.
*/
inline auto ReadDescriptorString(const std::span<const std::byte> bytes,
  const data::pak::core::StringTableOffsetT offset) -> std::string
{
  if (bytes.size() < sizeof(data::pak::core::AssetHeader)) {
    throw std::invalid_argument("descriptor is smaller than its header");
  }
  auto header = data::pak::core::AssetHeader {};
  std::memcpy(&header, bytes.data(), sizeof(header));
  return std::string {
    data::DescriptorStrings::FromDescriptor(bytes, header.strings).Get(offset),
  };
}

//! A minimal current-format scene for planner tests that do not need nodes.
[[nodiscard]] inline auto MakeEmptySceneDescriptor() -> std::vector<std::byte>
{
  return SceneDescriptor("Empty").bytes;
}

} // namespace oxygen::content::test
