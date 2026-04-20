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

class AtmosphereSkyViewLutPass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    ShaderVisibleIndex sky_view_lut_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex sky_view_lut_uav { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t dispatch_count_x { 0U };
    std::uint32_t dispatch_count_y { 0U };
    std::uint32_t dispatch_count_z { 0U };
  };

  OXGN_VRTX_API explicit AtmosphereSkyViewLutPass(Renderer& renderer);
  OXGN_VRTX_API ~AtmosphereSkyViewLutPass();

  AtmosphereSkyViewLutPass(const AtmosphereSkyViewLutPass&) = delete;
  auto operator=(const AtmosphereSkyViewLutPass&) -> AtmosphereSkyViewLutPass&
    = delete;
  AtmosphereSkyViewLutPass(AtmosphereSkyViewLutPass&&) = delete;
  auto operator=(AtmosphereSkyViewLutPass&&) -> AtmosphereSkyViewLutPass&
    = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    const EnvironmentViewData& view_data,
    const internal::StableAtmosphereState& stable_state) -> RecordState;

private:
  struct alignas(16) PassConstants {
    std::uint32_t output_texture_uav { 0U };
    std::uint32_t output_width { 0U };
    std::uint32_t output_height { 0U };
    std::uint32_t _pad0 { 0U };
    float sky_luminance_factor_height_fog_contribution[4] { 1.0F, 1.0F, 1.0F,
      1.0F };
    float planet_radius_atmosphere_height_camera_altitude_trace_scale[4] {
      6360000.0F,
      100000.0F,
      0.0F,
      1.0F,
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
