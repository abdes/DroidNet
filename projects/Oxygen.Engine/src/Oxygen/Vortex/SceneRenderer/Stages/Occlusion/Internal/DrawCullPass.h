//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {
struct PreparedSceneFrame;
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::vortex::occlusion::internal {

//! Mirrors OcclusionCullPassConstants in OcclusionCull.hlsl.
struct alignas(16) OcclusionCullPassConstants {
  glm::mat4 view_projection { 1.0F };
  //! Viewport origin and size, in pixels.
  glm::vec4 viewport { 0.0F };
  //! Rasterized pixel rect: min x, min y, max x, max y (exclusive).
  glm::vec4 clip_rect { 0.0F };
  ShaderVisibleIndex draw_metadata_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex cull_records_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex worlds_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex visibility_uav { kInvalidShaderVisibleIndex };
  std::uint32_t draw_count { 0U };
  std::uint32_t _pad0 { 0U };
  std::uint32_t _pad1 { 0U };
  std::uint32_t _pad2 { 0U };
};
static_assert(
  sizeof(OcclusionCullPassConstants) == 128U); // NOLINT(*-magic-numbers)

//! The view and draws one cull dispatch tests.
struct DrawCullInputs {
  frame::SequenceNumber frame_sequence { 0U };
  frame::Slot frame_slot { frame::kInvalidSlot };
  observer_ptr<const PreparedSceneFrame> prepared_frame;
  //! The matrix the rasterizer uses: projection (jitter included) times view.
  glm::mat4 view_projection { 1.0F };
  //! The viewport and scissors the passes rasterize with, already clamped.
  ViewPort viewport {};
  Scissors scissors {};
};

//! Culls one view's draws on the GPU, one thread per draw.
/*!
 Writes each draw's `DrawVisibilityBit`s: a draw is in the frustum when its
 box intersects the clip volume and covers a pixel center of the scissor rect.
 Occlusion is not tested, so every draw in the frustum is drawn by phase 1.

 Each `Run` in a frame gets its own visibility buffer, valid until the end of
 the frame. Buffers are reused in later frames.
*/
class DrawCullPass {
public:
  OXGN_VRTX_API DrawCullPass(Renderer& renderer, std::string_view debug_name);
  OXGN_VRTX_API ~DrawCullPass();

  OXYGEN_MAKE_NON_COPYABLE(DrawCullPass)
  OXYGEN_MAKE_NON_MOVABLE(DrawCullPass)

  //! Records the cull dispatch; invalid products when it cannot run.
  OXGN_VRTX_API auto Run(graphics::CommandRecorder& recorder,
    const DrawCullInputs& inputs) -> DrawVisibilityProducts;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::occlusion::internal
