//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

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

} // namespace oxygen::content::import::adapters
