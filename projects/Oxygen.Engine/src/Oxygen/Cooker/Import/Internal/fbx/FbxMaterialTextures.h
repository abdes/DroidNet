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

#include <Oxygen/Cooker/Import/Internal/fbx/FbxShadingModel.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxTextureSlot.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>

namespace oxygen::content::import::adapters {

//! Every texture the FBX adapter imports into a material slot.
/*!
 The adapter binds and cooks these, and the dropped-feature report names any
 other texture a material uses, so the three agree. A slot reads the ufbx map
 of its own meaning first; transmission falls back to the colour map a file
 may use instead. Specular, transmission and coverage read the maps the
 material's shader means by them (`ShadingModelOf`); coverage, the opacity
 map, becomes the alpha of the base colour map.
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
  //! The opacity map, which becomes the alpha of the base colour map; none
  //! when there is no base colour map or the two sample unlike.
  FbxTextureSlot coverage {};
  //! How the opacity map's value becomes that alpha.
  CoverageMapping coverage_mapping {};

  [[nodiscard]] static auto From(const ufbx_material& material) noexcept
    -> FbxMaterialTextures
  {
    const auto& model = ShadingModelOf(material);
    auto textures = FbxMaterialTextures {
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
      .specular = model.SpecularSlot(material),
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
      .transmission = model.TransmissionSlot(material),
    };
    // The coverage becomes the alpha of the base colour map's texels: it
    // needs one, sampled alike.
    const auto opacity = model.Opacity(material);
    const auto* base = textures.base_color.Texture();
    const auto* coverage = opacity.map.Texture();
    if (base != nullptr && coverage != nullptr
      && SamplesAlike(*base, *coverage)) {
      textures.coverage = opacity.map;
      textures.coverage_mapping = opacity.mapping;
    }
    return textures;
  }

  //! Whether `first` and `second` read the same texel of a surface point:
  //! one UV set, one UV transform, one wrap.
  [[nodiscard]] static auto SamplesAlike(
    const ufbx_texture& first, const ufbx_texture& second) noexcept -> bool
  {
    const auto same_vec3 = [](const ufbx_vec3& a, const ufbx_vec3& b) -> bool {
      return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    const auto& one = first.uv_transform;
    const auto& two = second.uv_transform;
    const auto same_transform
      = first.has_uv_transform == second.has_uv_transform
      && (!first.has_uv_transform
        || (same_vec3(one.translation, two.translation)
          && same_vec3(one.scale, two.scale) && one.rotation.x == two.rotation.x
          && one.rotation.y == two.rotation.y
          && one.rotation.z == two.rotation.z
          && one.rotation.w == two.rotation.w));
    const auto same_set
      = std::string_view(first.uv_set.data, first.uv_set.length)
      == std::string_view(second.uv_set.data, second.uv_set.length);
    const auto same_wrap
      = first.wrap_u == second.wrap_u && first.wrap_v == second.wrap_v;
    return same_set && same_transform && same_wrap;
  }

  [[nodiscard]] auto Slots() const noexcept -> std::array<FbxTextureSlot, 12>
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
      coverage,
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
