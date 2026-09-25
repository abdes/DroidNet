//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

class Renderer;

namespace shadows {
  namespace internal {
    struct ShadowMapVersion;
  }

  class ShadowDepthPass {
  public:
    struct RenderState {
      std::uint32_t rendered_cascade_count { 0U };
      std::uint32_t rendered_draw_count { 0U };
      std::uint32_t shadow_caster_draw_count { 0U };
      bool recording_succeeded { false };
      bool reused_depths { false };
    };

    struct DepthSlice {
      glm::mat4 light_view_projection { 1.0F };
      glm::vec4 shadow_bias_parameters { 0.0F };
      glm::vec4 light_direction_to_source { 0.0F, -1.0F, 0.0F, 0.0F };
      glm::vec4 light_position_and_inv_range { 0.0F };
      std::uint32_t target_slice { 0U };
      scene::NodeHandle light_source;
      std::uint32_t slot_generation { 0U };
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
      const ShadowFrameData& frame_data, const glm::vec3& light_direction)
      -> RenderState;
    [[nodiscard]] OXGN_VRTX_API auto RecordSlices(
      const PreparedViewShadowInput& view_input,
      const std::shared_ptr<graphics::Texture>& shadow_surface,
      std::span<const DepthSlice> depth_slices,
      const std::shared_ptr<internal::ShadowMapVersion>& local_map = {})
      -> RenderState;
    [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastRenderState() const noexcept
      -> const RenderState&
    {
      return last_render_state_;
    }

  private:
    struct SurfaceViews;
    Renderer& renderer_;
    frame::SequenceNumber current_sequence_ { 0U };
    frame::Slot current_slot_ { frame::kInvalidSlot };
    RenderState last_render_state_ {};
    upload::TransientStructuredBuffer pass_constants_buffer_;
    std::map<std::pair<const graphics::Texture*, std::uint32_t>,
      std::shared_ptr<SurfaceViews>>
      surface_views_;
  };

} // namespace shadows
} // namespace oxygen::vortex
