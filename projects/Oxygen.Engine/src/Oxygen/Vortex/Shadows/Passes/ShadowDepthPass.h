//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

class Renderer;

namespace shadows {

class ShadowDepthPass {
public:
  struct RenderState {
    std::uint32_t rendered_cascade_count { 0U };
    std::uint32_t rendered_draw_count { 0U };
  };

  struct DepthSlice {
    glm::mat4 light_view_projection { 1.0F };
    glm::vec4 shadow_bias_parameters { 0.0F };
    glm::vec4 light_direction_to_source { 0.0F, -1.0F, 0.0F, 0.0F };
    glm::vec4 light_position_and_inv_range { 0.0F };
    std::uint32_t target_slice { 0U };
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
    const std::shared_ptr<graphics::Texture>& shadow_surface,
    const DirectionalShadowFrameData& frame_data,
    std::span<const DrawCommand> draw_commands) -> RenderState;
  [[nodiscard]] OXGN_VRTX_API auto RecordSlices(
    const PreparedViewShadowInput& view_input,
    const std::shared_ptr<graphics::Texture>& shadow_surface,
    std::span<const DepthSlice> depth_slices,
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
  upload::TransientStructuredBuffer pass_constants_buffer_;
  std::vector<graphics::NativeView> cascade_dsvs_ {};
  const graphics::Texture* cascade_dsv_surface_ { nullptr };
};

} // namespace shadows
} // namespace oxygen::vortex
