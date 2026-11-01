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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::cooker::testing {

struct DescriptorFixture final {
  std::vector<std::byte> bytes;
  data::AssetReferences references;
};

enum class MaterialVariant : uint8_t { kPlain, kWithShader };

//! Records in the asset header of the descriptor `bytes` the SHA-256 of
//! its bytes, taken with that field zeroed, as a cook does.
inline auto StampContentHash(std::vector<std::byte>& bytes) -> void
{
  constexpr auto kOffset = offsetof(data::pak::core::AssetHeader, content_hash);
  constexpr auto kSize = sizeof(data::pak::core::AssetHeader::content_hash);
  if (bytes.size() < kOffset + kSize) {
    throw std::invalid_argument("descriptor is smaller than its header");
  }
  const auto field = std::span(bytes).subspan(kOffset, kSize);
  std::ranges::fill(field, std::byte { 0 });
  const auto hash = base::ComputeSha256(bytes);
  std::ranges::copy(std::as_bytes(std::span(hash)), field.begin());
}

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
  StampContentHash(fixture.bytes);
  return fixture;
}

inline auto TexturedMaterialDescriptor(const std::string_view name,
  const ResourceIndexT target,
  const MaterialVariant variant = MaterialVariant::kPlain) -> DescriptorFixture
{
  auto fixture = MaterialDescriptor(name, variant);
  const auto read = serio::CopyAt<data::pak::render::MaterialAssetDesc>(
    std::span<const std::byte> { fixture.bytes });
  if (!read) {
    throw std::invalid_argument("descriptor is smaller than its record");
  }
  auto descriptor = *read;
  descriptor.base_color_texture = data::ResourceReferenceIndex { 0U };
  std::memcpy(fixture.bytes.data(), &descriptor, sizeof(descriptor));
  StampContentHash(fixture.bytes);
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

//! The physics sidecar of the scene `scene_key`, whose descriptor is
//! `scene_bytes`, as a cook makes it: the scene's key, node count and content
//! hash, and the bindings `bindings`; for a scene stored in another source.
inline auto PhysicsSidecarDescriptor(const data::AssetKey& scene_key,
  const std::span<const std::byte> scene_bytes,
  const PhysicsSidecarBuilder& bindings = {},
  const std::string_view name = "PhysicsScene") -> DescriptorFixture
{
  return {
    .bytes = bindings.Bytes(scene_key, scene_bytes, name),
    .references = bindings.References(scene_key),
  };
}

inline auto GeometryDescriptor(const std::string_view name) -> DescriptorFixture
{
  data::pak::geometry::GeometryAssetDesc descriptor {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kGeometry);
  descriptor.header.version = data::pak::geometry::kGeometryAssetVersion;
  descriptor.lod_count = 1U;
  descriptor.surface_count = 1U;
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
  const data::pak::geometry::SurfaceDesc surface {
    .surface_id = submesh.surface_id,
    .flags = data::pak::geometry::kSurfaceFlag_Visible
      | data::pak::geometry::kSurfaceFlag_CastsShadows,
  };
  DescriptorFixture fixture;
  AppendRecord(fixture.bytes, descriptor);
  AppendRecord(fixture.bytes, mesh);
  AppendRecord(fixture.bytes, submesh);
  AppendRecord(fixture.bytes, view);
  AppendRecord(fixture.bytes, surface);
  data::AppendDescriptorStrings(fixture.bytes, strings);
  StampContentHash(fixture.bytes);
  return fixture;
}

//! A scene with no nodes.
inline auto SceneDescriptor(const std::string_view name) -> DescriptorFixture
{
  const auto scene = SceneBuilder { std::string(name) };
  return { .bytes = scene.Bytes(), .references = scene.References() };
}

//! Resolves a string of the cooked descriptor `bytes` hold.
/*!
 @throw std::invalid_argument when the table or the offset is invalid.
*/
inline auto ReadDescriptorString(const std::span<const std::byte> bytes,
  const data::pak::core::StringTableOffsetT offset) -> std::string
{
  const auto header = serio::CopyAt<data::pak::core::AssetHeader>(bytes);
  if (!header) {
    throw std::invalid_argument("descriptor is smaller than its header");
  }
  return std::string {
    data::DescriptorStrings::FromDescriptor(bytes, header->strings).Get(offset),
  };
}

//! A minimal current-format scene for planner tests that do not need nodes.
[[nodiscard]] inline auto MakeEmptySceneDescriptor() -> std::vector<std::byte>
{
  return SceneDescriptor("Empty").bytes;
}

} // namespace oxygen::cooker::testing
