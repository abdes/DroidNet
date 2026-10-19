//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>

using oxygen::data::MaterialAsset;
namespace {

constexpr auto kDebugMaterialAssetKey = oxygen::data::AssetKey::FromBytes(
  std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {
    0x4d,
    0x41,
    0x54,
    0x5f,
    0x44,
    0x45,
    0x42,
    0x55,
    0x47,
    0x5f,
    0x5f,
    0x5f,
    0x5f,
    0x5f,
    0x5f,
    0x5f,
  });

} // namespace

//! Creates a default material for procedural meshes and fallback scenarios.
/*!
 Creates a simple default material with sensible fallback values for cases
 where no specific material is available (e.g., procedurally generated meshes,
 debug geometry, placeholder content).

 ### Default Material Properties
 - **Domain**: Opaque
 - **Base Color**: White (1.0, 1.0, 1.0, 1.0)
 - **Metalness**: 0.0 (non-metallic)
 - **Roughness**: 0.8 (fairly rough, diffuse-like)
 - **Normal Scale**: 1.0 (no normal scaling)
 - **AO**: 1.0 (no ambient occlusion)
 - **Textures**: All indices set to fallback; sampling is disabled via flag
 - **Shaders**: Empty (to be filled by rendering system)

 @return A shared pointer to a default MaterialAsset

 @note This returns a cached singleton instance. This avoids repeated
       allocations for common fallback paths (e.g., procedural meshes).

 @see CreateDebugMaterial() for debug/wireframe materials
*/
auto MaterialAsset::CreateDefault() -> std::shared_ptr<const MaterialAsset>
{
  static const std::shared_ptr<const MaterialAsset> kDefaultMaterial
    = []() -> std::shared_ptr<const MaterialAsset> {
    pak::render::MaterialAssetDesc desc {};

    // Asset header - mark as procedural/default
    desc.header.asset_type
      = static_cast<uint8_t>(AssetType::kMaterial); // MaterialAsset type
    desc.header.version = pak::render::kMaterialAssetVersion;
    desc.header.streaming_priority = 255; // Lowest priority
    desc.header.content_hash = {}; // No specific content hash
    desc.header.variant_flags = 0;

    // Material properties
    desc.material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque);
    desc.flags = pak::render::kMaterialFlag_NoTextureSampling;
    desc.shader_stages = 0; // No shaders initially (filled by renderer)

    // PBR material values - neutral defaults
    desc.base_color[0] = 1.0F; // R
    desc.base_color[1] = 1.0F; // G
    desc.base_color[2] = 1.0F; // B
    desc.base_color[3] = 1.0F; // A
    desc.normal_scale = 1.0F;
    desc.metalness = Unorm16 { 0.0F }; // Non-metallic
    desc.roughness = Unorm16 { 0.8F }; // Fairly rough (diffuse-like)
    desc.ambient_occlusion = Unorm16 { 1.0F }; // No AO

    desc.uv_scale[0] = 1.0F;
    desc.uv_scale[1] = 1.0F;
    desc.uv_offset[0] = 0.0F;
    desc.uv_offset[1] = 0.0F;
    desc.uv_rotation_radians = 0.0F;
    desc.uv_set = 0;

    // No shader references initially - renderer will provide appropriate
    // shaders
    std::vector<ShaderReference> shader_refs {};

    auto material = std::make_shared<MaterialAsset>(
      AssetKey::FromVirtualPath(kDefaultVirtualPath), desc,
      std::move(shader_refs));
    material->SetAssetName("Default");
    return material;
  }();

  return kDefaultMaterial;
}

//! Creates a debug/wireframe material for development and debugging.
/*!
 Creates a bright debug material typically used for wireframe rendering,
 bounding box visualization, debug geometry, and development aids.

 ### Debug Material Properties
 - **Base Color**: Bright magenta (1.0, 0.0, 1.0, 1.0) - highly visible
 - **Metalness**: 0.0 (non-metallic)
 - **Roughness**: 1.0 (fully rough)
 - Other properties same as default

 @return A shared pointer to a debug MaterialAsset

 @note The bright magenta color is intentionally garish to make it obvious
       when debug materials are being used in production.
*/
auto MaterialAsset::CreateDebug() -> std::shared_ptr<const MaterialAsset>
{
  auto material = CreateDefault();

  // Copy the default and modify for debug use
  pak::render::MaterialAssetDesc debug_desc = material->desc_;

  // Bright magenta color - highly visible for debugging
  debug_desc.base_color[0] = 1.0F; // R - full red
  debug_desc.base_color[1] = 0.0F; // G - no green
  debug_desc.base_color[2] = 1.0F; // B - full blue
  debug_desc.base_color[3] = 1.0F; // A - fully opaque
  debug_desc.roughness = Unorm16 { 1.0F }; // Fully rough (no reflections)

  auto debug = std::make_shared<MaterialAsset>(
    kDebugMaterialAssetKey, debug_desc, std::vector<ShaderReference> {});
  debug->SetAssetName("Debug");
  return debug;
}
