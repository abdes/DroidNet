//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace shadows {

class ShadowDepthPass {
public:
  struct RenderState {
    std::uint32_t rendered_cascade_count { 0U };
    std::uint32_t rendered_draw_count { 0U };
  };

  OXGN_VRTX_API explicit ShadowDepthPass(Renderer& renderer);
  OXGN_VRTX_API ~ShadowDepthPass();

  ShadowDepthPass(const ShadowDepthPass&) = delete;
  auto operator=(const ShadowDepthPass&) -> ShadowDepthPass& = delete;
  ShadowDepthPass(ShadowDepthPass&&) = delete;
  auto operator=(ShadowDepthPass&&) -> ShadowDepthPass& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] OXGN_VRTX_API auto Record(
    const PreparedViewShadowInput& view_input,
    const DirectionalShadowFrameData& frame_data,
    std::span<const DrawCommand> draw_commands) -> RenderState;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastRenderState() const noexcept
    -> const RenderState&
  {
    return last_render_state_;
  }

private:
  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  RenderState last_render_state_ {};
};

} // namespace shadows
} // namespace oxygen::vortex
