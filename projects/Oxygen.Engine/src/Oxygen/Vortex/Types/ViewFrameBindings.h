//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

//! Top-level bindless system routing payload for a single view.
/*!
 Published as a structured buffer element and referenced from ViewConstants.
 Descriptor fields route system-owned frame payloads. Lighting also carries
 the expected publication generation so a stale descriptor cannot authorize it.
*/
struct alignas(16) ViewFrameBindings {
  ShaderVisibleIndex draw_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex lighting_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex environment_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex frame_exposure_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex scene_texture_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex scene_depth_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex screen_hzb_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex shadow_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex virtual_shadow_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex post_process_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex debug_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex history_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex ray_tracing_frame_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex exposure_status_uav { kInvalidShaderVisibleIndex };
  //! Expected lighting publication identity, distinct from descriptor indices.
  std::array<std::uint32_t, 2> lighting_view_generation {};
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(sizeof(ViewFrameBindings) == 64U);
static_assert(alignof(ViewFrameBindings) == 16U);
static_assert(std::is_standard_layout_v<ViewFrameBindings>);
static_assert(std::is_trivially_copyable_v<ViewFrameBindings>);
static_assert(offsetof(ViewFrameBindings, draw_frame_slot) == 0U);
static_assert(offsetof(ViewFrameBindings, lighting_frame_slot) == 4U);
static_assert(offsetof(ViewFrameBindings, environment_frame_slot) == 8U);
static_assert(offsetof(ViewFrameBindings, frame_exposure_slot) == 12U);
static_assert(offsetof(ViewFrameBindings, scene_texture_frame_slot) == 16U);
static_assert(offsetof(ViewFrameBindings, scene_depth_slot) == 20U);
static_assert(offsetof(ViewFrameBindings, screen_hzb_frame_slot) == 24U);
static_assert(offsetof(ViewFrameBindings, shadow_frame_slot) == 28U);
static_assert(offsetof(ViewFrameBindings, virtual_shadow_frame_slot) == 32U);
static_assert(offsetof(ViewFrameBindings, post_process_frame_slot) == 36U);
static_assert(offsetof(ViewFrameBindings, debug_frame_slot) == 40U);
static_assert(offsetof(ViewFrameBindings, history_frame_slot) == 44U);
static_assert(offsetof(ViewFrameBindings, ray_tracing_frame_slot) == 48U);
static_assert(offsetof(ViewFrameBindings, exposure_status_uav) == 52U);
static_assert(offsetof(ViewFrameBindings, lighting_view_generation) == 56U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
