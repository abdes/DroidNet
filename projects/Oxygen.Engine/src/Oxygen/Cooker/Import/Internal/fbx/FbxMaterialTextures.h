//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>

#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>

namespace oxygen::content::import::adapters {

//! One native texture semantic with its native FBX property alternative.
struct FbxTextureSlot final {
  const ufbx_material_map* primary = nullptr;
  const ufbx_material_map* fallback = nullptr;

  [[nodiscard]] static auto Active(const ufbx_material_map& map) noexcept
    -> bool
  {
    return !map.feature_disabled && map.texture_enabled
      && map.texture != nullptr;
  }

  [[nodiscard]] auto Texture() const noexcept -> const ufbx_texture*
  {
    // A disabled primary binding must not be resurrected by its legacy alias.
    if (primary != nullptr
      && (primary->texture != nullptr || primary->feature_disabled)) {
      return Active(*primary) ? primary->texture : nullptr;
    }
    return fallback != nullptr && Active(*fallback) ? fallback->texture
                                                    : nullptr;
  }

  [[nodiscard]] auto Recognizes(const ufbx_material_map& map) const noexcept
    -> bool
  {
    return primary == &map || fallback == &map;
  }
};

//! The texture semantics actually emitted by the FBX adapter.
struct FbxMaterialTextures final {
  FbxTextureSlot base_color {};
  FbxTextureSlot normal {};
  FbxTextureSlot metallic {};
  FbxTextureSlot roughness {};
  FbxTextureSlot occlusion {};
  FbxTextureSlot emissive {};

  [[nodiscard]] static auto From(const ufbx_material& material) noexcept
    -> FbxMaterialTextures
  {
    return {
      .base_color = { &material.pbr.base_color, &material.fbx.diffuse_color },
      .normal = { &material.pbr.normal_map, &material.fbx.normal_map },
      .metallic = { &material.pbr.metalness, nullptr },
      .roughness = { &material.pbr.roughness, nullptr },
      .occlusion = { &material.pbr.ambient_occlusion, nullptr },
      .emissive
      = { &material.pbr.emission_color, &material.fbx.emission_color },
    };
  }

  [[nodiscard]] auto Slots() const noexcept -> std::array<FbxTextureSlot, 6>
  {
    return { base_color, normal, metallic, roughness, occlusion, emissive };
  }

  [[nodiscard]] auto Supports(const ufbx_material_map& map) const noexcept
    -> bool
  {
    return std::ranges::any_of(
      Slots(), [&](const auto& slot) { return slot.Recognizes(map); });
  }

  [[nodiscard]] auto Any() const noexcept -> bool
  {
    return std::ranges::any_of(
      Slots(), [](const auto& slot) { return slot.Texture() != nullptr; });
  }
};

} // namespace oxygen::content::import::adapters
