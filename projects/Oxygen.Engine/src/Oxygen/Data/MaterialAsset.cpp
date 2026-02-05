//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Data/MaterialAsset.h>

using oxygen::data::MaterialAsset;
namespace {

constexpr auto kDefaultMaterialAssetKey = oxygen::data::AssetKey { {
  0x4d,
  0x41,
  0x54,
  0x5f,
  0x44,
  0x45,
  0x46,
  0x41,
  0x55,
  0x4c,
  0x54,
  0x5f,
  0x5f,
  0x5f,
  0x5f,
  0x5f,
} };

constexpr auto kDebugMaterialAssetKey = oxygen::data::AssetKey { {
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
} };

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
    pak::MaterialAssetDesc desc {};

    // Asset header - mark as procedural/default
    desc.header.asset_type
      = static_cast<uint8_t>(AssetType::kMaterial); // MaterialAsset type
    // Safe string copy with null termination
    constexpr const char* default_name = "Default";
    std::size_t copy_len
      = (std::min)(sizeof(desc.header.name) - 1, std::strlen(default_name));
    std::memcpy(desc.header.name, default_name, copy_len);
    desc.header.name[copy_len] = '\0';
    desc.header.version = pak::kMaterialAssetVersion;
    desc.header.streaming_priority = 255; // Lowest priority
    desc.header.content_hash = 0; // No specific content hash
    desc.header.variant_flags = 0;

    // Material properties
    desc.material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque);
    desc.flags = pak::kMaterialFlag_NoTextureSampling;
    desc.shader_stages = 0; // No shaders initially (filled by renderer)

    // PBR material values - neutral defaults
    desc.base_color[0] = 1.0f; // R
    desc.base_color[1] = 1.0f; // G
    desc.base_color[2] = 1.0f; // B
    desc.base_color[3] = 1.0f; // A
    desc.normal_scale = 1.0f;
    desc.metalness = Unorm16 { 0.0f }; // Non-metallic
    desc.roughness = Unorm16 { 0.8f }; // Fairly rough (diffuse-like)
    desc.ambient_occlusion = Unorm16 { 1.0f }; // No AO

    // Texture indices - fallback texture. Sampling is disabled via flags.
    desc.base_color_texture = pak::kFallbackResourceIndex;
    desc.normal_texture = pak::kFallbackResourceIndex;
    desc.metallic_texture = pak::kFallbackResourceIndex;
    desc.roughness_texture = pak::kFallbackResourceIndex;
    desc.ambient_occlusion_texture = pak::kFallbackResourceIndex;

    // Tier 1/2 texture indices - fallback texture. Sampling is disabled via
    // flags.
    desc.emissive_texture = pak::kFallbackResourceIndex;
    desc.specular_texture = pak::kFallbackResourceIndex;
    desc.sheen_color_texture = pak::kFallbackResourceIndex;
    desc.clearcoat_texture = pak::kFallbackResourceIndex;
    desc.clearcoat_normal_texture = pak::kFallbackResourceIndex;
    desc.transmission_texture = pak::kFallbackResourceIndex;
    desc.thickness_texture = pak::kFallbackResourceIndex;

    desc.uv_scale[0] = 1.0f;
    desc.uv_scale[1] = 1.0f;
    desc.uv_offset[0] = 0.0f;
    desc.uv_offset[1] = 0.0f;
    desc.uv_rotation_radians = 0.0f;
    desc.uv_set = 0;
    std::fill(
      std::begin(desc.reserved), std::end(desc.reserved), uint8_t { 0 });

    // No shader references initially - renderer will provide appropriate
    // shaders
    std::vector<ShaderReference> shader_refs {};

    return std::make_shared<const MaterialAsset>(
      kDefaultMaterialAssetKey, std::move(desc), std::move(shader_refs));
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
  pak::MaterialAssetDesc debug_desc = material->desc_;
  // Safe string copy with null termination
  constexpr const char* debug_name = "Debug";
  std::size_t copy_len
    = (std::min)(sizeof(debug_desc.header.name) - 1, std::strlen(debug_name));
  std::memcpy(debug_desc.header.name, debug_name, copy_len);
  debug_desc.header.name[copy_len] = '\0';

  // Bright magenta color - highly visible for debugging
  debug_desc.base_color[0] = 1.0f; // R - full red
  debug_desc.base_color[1] = 0.0f; // G - no green
  debug_desc.base_color[2] = 1.0f; // B - full blue
  debug_desc.base_color[3] = 1.0f; // A - fully opaque
  debug_desc.roughness = Unorm16 { 1.0f }; // Fully rough (no reflections)

  return std::make_shared<const MaterialAsset>(kDebugMaterialAssetKey,
    std::move(debug_desc), std::vector<ShaderReference> {});
}
