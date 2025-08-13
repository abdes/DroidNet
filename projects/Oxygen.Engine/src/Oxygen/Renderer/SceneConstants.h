//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Per-frame scene (view) constants snapshot uploaded once each frame.
/*!
 Layout mirrors the HLSL cbuffer SceneConstants (b1, space0). This is a
 snapshot: call Renderer::SetSceneConstants() exactly once per frame before
 ExecuteRenderGraph. Subsequent calls in the same frame overwrite previous
 values (last-wins). Partial / per-field mutation APIs are intentionally omitted
 in Phase 1 to enforce deterministic content and simplify dirty tracking.

 world_matrix is deliberately NOT included: object transforms are per-item
 (RenderItem.world_transform) and will be consumed by later pipeline stages
 (DrawPacket). Shaders temporarily treat object space == world space until
 per-item matrix binding is added in a later phase.

 Fields:
   - view_matrix / projection_matrix: Camera basis.
   - camera_position: World-space camera origin.
   - time_seconds: Accumulated time (seconds) for temporal effects.
   - frame_index: Monotonic frame counter.

 Alignment: Each glm::mat4 occupies 64 bytes (column-major). frame_index is a
 32-bit value that begins a 16-byte register; we fill the remaining 12 bytes
 of that register with three 32-bit reserved slots so the total struct size
 stays a multiple of 16 bytes (root CBV requirement on D3D12).
*/
struct SceneConstants {
  glm::mat4 view_matrix { 1.0f };
  glm::mat4 projection_matrix { 1.0f };
  glm::vec3 camera_position { 0.0f, 0.0f, 0.0f };
  float time_seconds { 0.0f };
  std::uint32_t frame_index { 0 };
  std::uint32_t _reserved[3] { 0, 0, 0 }; // padding / future expansion
};
static_assert(sizeof(SceneConstants) % 16 == 0,
  "SceneConstants size must be 16-byte aligned");

} // namespace oxygen::engine
