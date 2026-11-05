//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>

namespace oxygen::content::import::adapters {

//! One native texture semantic with its native FBX property alternative.
struct FbxTextureSlot final {
  const ufbx_material_map* primary = nullptr;
  const ufbx_material_map* fallback = nullptr;
  //! The material feature the slot belongs to; a disabled one imports none.
  const ufbx_material_feature_info* feature = nullptr;

  [[nodiscard]] static auto Active(const ufbx_material_map& map) noexcept
    -> bool
  {
    return !map.feature_disabled && map.texture_enabled
      && map.texture != nullptr;
  }

  [[nodiscard]] auto Texture() const noexcept -> const ufbx_texture*
  {
    if (feature != nullptr && !feature->enabled) {
      return nullptr;
    }
    // A disabled primary binding must not be resurrected by its legacy alias.
    if (primary != nullptr
      && (primary->texture != nullptr || primary->feature_disabled)) {
      return Active(*primary) ? primary->texture : nullptr;
    }
    return fallback != nullptr && Active(*fallback) ? fallback->texture
                                                    : nullptr;
  }
};

//! What a material's specular means, which its shader decides.
enum class FbxSpecular : uint8_t {
  //! The shape and strength of a highlight (FBX Phong or Lambert, or an
  //! unknown shader): not a reflectance, so not imported.
  kHighlight,
  //! Blender's specular level, the format's own measure.
  kLevel,
  //! A PBR shader's specular weight: 1.0 is the default reflectance.
  kWeight,
};

//! What `material`'s specular means.
[[nodiscard]] inline auto SpecularOf(const ufbx_material& material) noexcept
  -> FbxSpecular
{
  switch (material.shader_type) {
  case UFBX_SHADER_BLENDER_PHONG:
    return FbxSpecular::kLevel;
  case UFBX_SHADER_UNKNOWN:
  case UFBX_SHADER_FBX_LAMBERT:
  case UFBX_SHADER_FBX_PHONG:
    return FbxSpecular::kHighlight;
  default:
    return FbxSpecular::kWeight;
  }
}

//! Every texture the FBX adapter imports into a material slot.
/*!
 The adapter binds and cooks these, and the dropped-feature report names any
 other texture a material uses, so the three agree. A slot reads the ufbx map
 of its own meaning first; transmission falls back to the colour map a file
 may use instead. Specular reads the map its shader's specular means
 (`SpecularOf`), and none for a highlight.
*/
struct FbxMaterialTextures final {
  FbxTextureSlot base_color {};
  FbxTextureSlot normal {};
  FbxTextureSlot metallic {};
  FbxTextureSlot roughness {};
  FbxTextureSlot occlusion {};
  FbxTextureSlot emissive {};
  FbxTextureSlot specular {};
  FbxTextureSlot sheen_color {};
  FbxTextureSlot clearcoat {};
  FbxTextureSlot clearcoat_normal {};
  FbxTextureSlot transmission {};

  [[nodiscard]] static auto From(const ufbx_material& material) noexcept
    -> FbxMaterialTextures
  {
    return {
      .base_color = { .primary=&material.pbr.base_color, .fallback=&material.fbx.diffuse_color },
      .normal = { .primary=&material.pbr.normal_map, .fallback=&material.fbx.normal_map },
      .metallic = { .primary=&material.pbr.metalness, .fallback=nullptr },
      // ufbx moves a material's glossiness out of its roughness map.
      .roughness = {
        .primary = material.features.roughness_as_glossiness.enabled
          ? &material.pbr.glossiness
          : &material.pbr.roughness,
        .fallback = nullptr,
      },
      .occlusion = { .primary=&material.pbr.ambient_occlusion, .fallback=nullptr },
      .emissive
      = { .primary=&material.pbr.emission_color, .fallback=&material.fbx.emission_color },
      .specular = SpecularSlot(material),
      .sheen_color = {
        .primary = &material.pbr.sheen_color,
        .fallback = nullptr,
        .feature = &material.features.sheen,
      },
      .clearcoat = {
        .primary = &material.pbr.coat_factor,
        .fallback = nullptr,
        .feature = &material.features.coat,
      },
      .clearcoat_normal = {
        .primary = &material.pbr.coat_normal,
        .fallback = nullptr,
        .feature = &material.features.coat,
      },
      .transmission = {
        .primary = &material.pbr.transmission_factor,
        .fallback = &material.pbr.transmission_color,
        .feature = &material.features.transmission,
      },
    };
  }

  //! The map holding `material`'s specular, by what its specular means.
  [[nodiscard]] static auto SpecularSlot(const ufbx_material& material) noexcept
    -> FbxTextureSlot
  {
    switch (SpecularOf(material)) {
    case FbxSpecular::kLevel:
      return {
        .primary = &material.fbx.specular_factor,
        .fallback = nullptr,
        .feature = nullptr,
      };
    case FbxSpecular::kWeight:
      return {
        .primary = &material.pbr.specular_factor,
        .fallback = nullptr,
        .feature = &material.features.specular,
      };
    case FbxSpecular::kHighlight:
      break;
    }
    return {};
  }

  [[nodiscard]] auto Slots() const noexcept -> std::array<FbxTextureSlot, 11>
  {
    return {
      base_color,
      normal,
      metallic,
      roughness,
      occlusion,
      emissive,
      specular,
      sheen_color,
      clearcoat,
      clearcoat_normal,
      transmission,
    };
  }

  //! Whether a slot imports `texture`.
  [[nodiscard]] auto Carries(const ufbx_texture& texture) const noexcept -> bool
  {
    return std::ranges::any_of(Slots(),
      [&](const auto& slot) -> auto { return slot.Texture() == &texture; });
  }

  //! Whether a slot imports the texture of `map` from that very map.
  [[nodiscard]] auto Reads(const ufbx_material_map& map) const noexcept -> bool
  {
    return std::ranges::any_of(Slots(), [&](const auto& slot) -> auto {
      return (slot.primary == &map || slot.fallback == &map)
        && slot.Texture() == map.texture;
    });
  }

  [[nodiscard]] auto Any() const noexcept -> bool
  {
    return std::ranges::any_of(Slots(),
      [](const auto& slot) -> auto { return slot.Texture() != nullptr; });
  }
};

[[nodiscard]] inline auto SameName(
  const ufbx_string& first, const ufbx_string& second) noexcept -> bool
{
  return std::string_view(first.data, first.length)
    == std::string_view(second.data, second.length);
}

//! The materials a node's mesh draws with.
[[nodiscard]] inline auto MaterialsOf(const ufbx_node& node) noexcept
  -> std::span<ufbx_material* const>
{
  const auto& materials = node.materials.count != 0U || node.mesh == nullptr
    ? node.materials
    : node.mesh->materials;
  return { materials.data, materials.count };
}

//! The UV set a mesh keeps beside its first: the one a texture of `materials`
//! names, or else its second. Null for a mesh of one set.
[[nodiscard]] inline auto SecondUvSetOf(const ufbx_mesh& mesh,
  const std::span<ufbx_material* const> materials) -> const ufbx_uv_set*
{
  const auto sets = std::span(mesh.uv_sets.data, mesh.uv_sets.count);
  if (sets.size() < 2U) {
    return nullptr;
  }
  const auto others = sets.subspan(1U);
  for (const auto* material : materials) {
    if (material == nullptr) {
      continue;
    }
    for (const auto& slot : FbxMaterialTextures::From(*material).Slots()) {
      const auto* texture = slot.Texture();
      if (texture == nullptr) {
        continue;
      }
      const auto named
        = std::ranges::find_if(others, [&](const ufbx_uv_set& set) -> bool {
            return SameName(set.name, texture->uv_set);
          });
      if (named != others.end()) {
        return &*named;
      }
    }
  }
  return &others.front();
}

//! The first mesh of the scene that draws with each material.
[[nodiscard]] inline auto FirstMeshOfMaterials(const ufbx_scene& scene)
  -> std::unordered_map<const ufbx_material*, const ufbx_mesh*>
{
  auto meshes = std::unordered_map<const ufbx_material*, const ufbx_mesh*> {};
  for (const auto* node : std::span(scene.nodes.data, scene.nodes.count)) {
    if (node->mesh == nullptr) {
      continue;
    }
    for (const auto* material : MaterialsOf(*node)) {
      meshes.try_emplace(material, node->mesh);
    }
  }
  return meshes;
}

//! The UV set a material binding reads for `texture`: 1 when it names a set
//! other than the first of `mesh`, the first mesh drawing with the material.
//! A name no set has reads the first.
[[nodiscard]] inline auto BoundUvSet(
  const ufbx_texture& texture, const ufbx_mesh* mesh) -> uint8_t
{
  if (mesh == nullptr || mesh->uv_sets.count < 2U
    || texture.uv_set.length == 0U) {
    return 0U;
  }
  const auto others
    = std::span(mesh->uv_sets.data, mesh->uv_sets.count).subspan(1U);
  return std::ranges::any_of(others,
           [&](const ufbx_uv_set& set) -> bool {
             return SameName(set.name, texture.uv_set);
           })
    ? uint8_t { 1 }
    : uint8_t { 0 };
}

} // namespace oxygen::content::import::adapters
