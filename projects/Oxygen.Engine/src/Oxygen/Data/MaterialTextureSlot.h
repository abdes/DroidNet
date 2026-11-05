//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oxygen::data {

//! The texture slots of a material, in the order the cooked record holds them.
enum class MaterialTextureSlot : std::uint8_t {
  kBaseColor,
  kNormal,
  kMetallic,
  kRoughness,
  kAmbientOcclusion,
  kEmissive,
  kSpecular,
  kSheenColor,
  kClearcoat,
  kClearcoatNormal,
  kTransmission,
  kThickness,
};

//! Texture slots in a material.
inline constexpr std::size_t kMaterialTextureSlotCount = 12U;

//! The slot's name, as a material descriptor spells it.
[[nodiscard]] constexpr auto to_string(const MaterialTextureSlot slot) noexcept
  -> std::string_view
{
  switch (slot) {
  case MaterialTextureSlot::kBaseColor:
    return "base_color";
  case MaterialTextureSlot::kNormal:
    return "normal";
  case MaterialTextureSlot::kMetallic:
    return "metallic";
  case MaterialTextureSlot::kRoughness:
    return "roughness";
  case MaterialTextureSlot::kAmbientOcclusion:
    return "ambient_occlusion";
  case MaterialTextureSlot::kEmissive:
    return "emissive";
  case MaterialTextureSlot::kSpecular:
    return "specular";
  case MaterialTextureSlot::kSheenColor:
    return "sheen_color";
  case MaterialTextureSlot::kClearcoat:
    return "clearcoat";
  case MaterialTextureSlot::kClearcoatNormal:
    return "clearcoat_normal";
  case MaterialTextureSlot::kTransmission:
    return "transmission";
  case MaterialTextureSlot::kThickness:
    return "thickness";
  }
  return "__NotSupported__";
}

} // namespace oxygen::data
