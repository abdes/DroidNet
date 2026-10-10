//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Texture;
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
  //! Projection terms P[2][2], P[3][2], P[2][3], P[3][3] (column, row).
  glm::vec4 depth_terms { 0.0F };
  //! The pyramid's source rect: origin x, origin y, width, height.
  glm::uvec4 pyramid_source { 0U };
  ShaderVisibleIndex draw_metadata_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex cull_records_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex worlds_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex visibility_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex history_slots_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex history_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex stats_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex pyramid_srv { kInvalidShaderVisibleIndex };
  std::uint32_t draw_count { 0U };
  std::uint32_t flags { 0U };
  float depth_bias { 0.0F };
  std::uint32_t _pad0 { 0U };
};
static_assert(
  sizeof(OcclusionCullPassConstants) == 176U); // NOLINT(*-magic-numbers)

//! A culling view's visibility history and counters, owned by the caller.
struct DrawCullHistory {
  //! One slot word per draw, from `HistorySlotAllocator::Assignment`.
  ShaderVisibleIndex slots_srv { kInvalidShaderVisibleIndex };
  //! One `uint` per slot: 1 when the draw was visible after the last phase 2.
  observer_ptr<const graphics::Buffer> history;
  ShaderVisibleIndex history_uav { kInvalidShaderVisibleIndex };
  //! False after a history reset: phase 1 then draws every draw in the
  //! frustum.
  bool valid { false };
  //! `OcclusionStats` counters, cleared by phase 1; null when not counted.
  observer_ptr<const graphics::Buffer> stats;
  ShaderVisibleIndex stats_uav { kInvalidShaderVisibleIndex };
};

//! The view and draws one cull tests.
struct DrawCullInputs {
  frame::SequenceNumber frame_sequence { 0U };
  frame::Slot frame_slot { frame::kInvalidSlot };
  observer_ptr<const PreparedSceneFrame> prepared_frame;
  glm::mat4 view_matrix { 1.0F };
  //! The projection the rasterizer uses, jitter included. Occlusion requires
  //! reversed-Z.
  glm::mat4 projection_matrix { 1.0F };
  //! The viewport and scissors the passes rasterize with, already clamped.
  ViewPort viewport {};
  Scissors scissors {};
  //! Without occlusion, phase 1 draws every draw in the frustum and is final.
  bool occlusion_enabled { false };
  //! Relative view-depth margin a box must lie behind its occluders.
  float depth_bias { 0.0F };
  DrawCullHistory history {};
};

//! The furthest pyramid of phase 1 depth, in `kShaderResource`.
struct OcclusionPyramidBinding {
  observer_ptr<const graphics::Texture> texture;
  ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  //! The source rect of the depth it reduces, in pixels.
  std::uint32_t origin_x { 0U };
  std::uint32_t origin_y { 0U };
  std::uint32_t width { 0U };
  std::uint32_t height { 0U };
};

//! Culls one view's draws on the GPU in two phases, one thread per draw.
/*!
 `RunPhase1` writes each draw's frustum bit: its oriented box intersects the
 clip volume and covers a pixel center of the scissor rect. A draw in the
 frustum is drawn in phase 1 when it was visible last frame, after a history
 reset, or always without occlusion.

 `RunPhase2` tests every draw in the frustum against the occlusion pyramid. A
 visible draw not drawn in phase 1 is drawn in phase 2, and visibility becomes
 the draw's history. Without a pyramid every draw in the frustum is visible.

 Each phase 1 in a frame gets its own visibility buffer, valid until the end
 of the frame. Buffers are reused in later frames.
*/
class DrawCullPass {
public:
  OXGN_VRTX_API DrawCullPass(Renderer& renderer, std::string_view debug_name);
  OXGN_VRTX_API ~DrawCullPass();

  OXYGEN_MAKE_NON_COPYABLE(DrawCullPass)
  OXYGEN_MAKE_NON_MOVABLE(DrawCullPass)

  //! Records phase 1; invalid products when it cannot run.
  OXGN_VRTX_API auto RunPhase1(graphics::CommandRecorder& recorder,
    const DrawCullInputs& inputs) -> DrawVisibilityProducts;

  //! Records phase 2 over `phase1`, the products of this pass's phase 1 for
  //! the same inputs this frame.
  OXGN_VRTX_API auto RunPhase2(graphics::CommandRecorder& recorder,
    const DrawCullInputs& inputs, const DrawVisibilityProducts& phase1,
    const std::optional<OcclusionPyramidBinding>& pyramid)
    -> DrawVisibilityProducts;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::occlusion::internal
