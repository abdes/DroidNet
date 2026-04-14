//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

//! Top-level bindless system routing payload for a single view.
/*!
 Published as a structured buffer element and referenced from ViewConstants.
 Each field is a shader-visible slot for a system-owned frame payload.
*/
struct alignas(16) ViewFrameBindings {
  ShaderVisibleIndex draw_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex lighting_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex environment_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex view_color_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex scene_texture_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex scene_depth_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex shadow_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex virtual_shadow_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex post_process_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex debug_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex history_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex ray_tracing_frame_slot { kInvalidShaderVisibleIndex };
};

static_assert(sizeof(ViewFrameBindings) == 48);
static_assert(alignof(ViewFrameBindings) == 16);
static_assert(sizeof(ViewFrameBindings) % 16 == 0);

} // namespace oxygen::vortex
