//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

//! A view's GPU debug buffers, routed by `ViewFrameBindings::debug_frame_slot`.
/*!
 Mirrors `GpuDebugBindings` in `Vortex/Contracts/Diagnostics/
 GpuDebugBindings.hlsli`.
*/
struct alignas(16) DebugFrameBindings {
  ShaderVisibleIndex line_buffer_srv_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex line_buffer_uav_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex counter_buffer_uav_slot { kInvalidShaderVisibleIndex };
  //! Lines the line buffer holds; producers count lines past it.
  std::uint32_t line_capacity { 0U };
};

static_assert(sizeof(DebugFrameBindings) == 16);
static_assert(alignof(DebugFrameBindings) == 16);

//! One world-space debug line; mirrors `GpuDebugLine` in `GpuDebug.hlsli`.
struct GpuDebugLine {
  glm::vec4 world_pos0 { 0.0F };
  glm::vec4 world_pos1 { 0.0F };
  glm::vec4 color0 { 0.0F };
  glm::vec4 color1 { 0.0F };
};

static_assert(sizeof(GpuDebugLine) == 64);

//! A view's GPU debug counters; mirrors the `GPU_DEBUG_COUNTER_*` words.
/*!
 The first four words are the `D3D12_DRAW_ARGUMENTS` of the line draw: two
 vertices per instance and one instance per stored line.
*/
struct GpuDebugCounters {
  std::uint32_t vertex_count { 0U };
  std::uint32_t drawn_lines { 0U };
  std::uint32_t start_vertex { 0U };
  std::uint32_t start_instance { 0U };
  //! Lines producers emitted, stored or not.
  std::uint32_t requested_lines { 0U };
  std::uint32_t invalid_bindless_accesses { 0U };
  std::uint32_t invalid_materials { 0U };
  //! The slot of the latest invalid access of either kind.
  std::uint32_t last_invalid_slot { 0U };
};

static_assert(sizeof(GpuDebugCounters) == 32);

} // namespace oxygen::vortex
