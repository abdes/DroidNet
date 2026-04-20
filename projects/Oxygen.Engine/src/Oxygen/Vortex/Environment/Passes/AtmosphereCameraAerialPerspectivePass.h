//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;

namespace environment::internal {
struct StableAtmosphereState;
}

namespace environment {

class AtmosphereCameraAerialPerspectivePass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    ShaderVisibleIndex camera_aerial_perspective_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex camera_aerial_perspective_uav { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t depth { 0U };
    std::uint32_t dispatch_count_x { 0U };
    std::uint32_t dispatch_count_y { 0U };
    std::uint32_t dispatch_count_z { 0U };
  };

  OXGN_VRTX_API explicit AtmosphereCameraAerialPerspectivePass(Renderer& renderer);
  OXGN_VRTX_API ~AtmosphereCameraAerialPerspectivePass();

  AtmosphereCameraAerialPerspectivePass(
    const AtmosphereCameraAerialPerspectivePass&) = delete;
  auto operator=(const AtmosphereCameraAerialPerspectivePass&)
    -> AtmosphereCameraAerialPerspectivePass& = delete;
  AtmosphereCameraAerialPerspectivePass(
    AtmosphereCameraAerialPerspectivePass&&) = delete;
  auto operator=(AtmosphereCameraAerialPerspectivePass&&)
    -> AtmosphereCameraAerialPerspectivePass& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    const EnvironmentViewData& view_data,
    const internal::StableAtmosphereState& stable_state,
    ShaderVisibleIndex sky_view_lut_srv) -> RecordState;

private:
  struct alignas(16) PassConstants {
    std::uint32_t output_texture_uav { 0U };
    std::uint32_t output_width { 0U };
    std::uint32_t output_height { 0U };
    std::uint32_t output_depth { 0U };
    std::uint32_t sky_view_lut_srv { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
    float sky_aerial_luminance_aerial_start_depth_m[4] { 1.0F, 1.0F, 1.0F,
      100.0F };
    float aerial_distance_scale_strength_camera_altitude[4] { 1.0F, 1.0F,
      0.0F, 1.0F };
    float planet_radius_atmosphere_height_height_fog_contribution_pad[4] {
      6360000.0F,
      100000.0F,
      1.0F,
      0.0F,
    };
    float sun_direction_ws_pad[4] { 0.0F, 0.8660254F, 0.5F, 0.0F };
    float sun_illuminance_rgb_pad[4] { 0.0F, 0.0F, 0.0F, 0.0F };
  };

  Renderer& renderer_;
  upload::TransientStructuredBuffer pass_constants_buffer_;
  std::vector<std::shared_ptr<graphics::Texture>> live_textures_ {};
};

} // namespace environment
} // namespace oxygen::vortex
