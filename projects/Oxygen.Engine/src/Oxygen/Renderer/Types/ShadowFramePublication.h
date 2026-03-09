//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::renderer {

//! Shader-facing per-view shadow publication.
/*!
 Owns only the bindless-facing publication needed for shading and top-level
 routing. It intentionally excludes backend render-planning data.
*/
struct ShadowFramePublication {
  ShaderVisibleIndex shadow_instance_metadata_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex directional_shadow_metadata_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex directional_shadow_texture_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_page_table_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_physical_pool_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_directional_shadow_metadata_srv {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t sun_shadow_index { 0xFFFFFFFFU };
};

} // namespace oxygen::renderer
