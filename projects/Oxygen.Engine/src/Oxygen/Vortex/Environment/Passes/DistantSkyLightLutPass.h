//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {
struct RenderContext;
class Renderer;

namespace environment::internal {
class AtmosphereLutCache;
struct StableAtmosphereState;
}

namespace environment {

class DistantSkyLightLutPass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    ShaderVisibleIndex distant_sky_light_lut_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex distant_sky_light_lut_uav { kInvalidShaderVisibleIndex };
    std::uint32_t dispatch_count_x { 0U };
    std::uint32_t dispatch_count_y { 0U };
    std::uint32_t dispatch_count_z { 0U };
  };

  OXGN_VRTX_API explicit DistantSkyLightLutPass(Renderer& renderer);
  OXGN_VRTX_API ~DistantSkyLightLutPass();

  DistantSkyLightLutPass(const DistantSkyLightLutPass&) = delete;
  auto operator=(const DistantSkyLightLutPass&)
    -> DistantSkyLightLutPass& = delete;
  DistantSkyLightLutPass(DistantSkyLightLutPass&&) = delete;
  auto operator=(DistantSkyLightLutPass&&)
    -> DistantSkyLightLutPass& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    const internal::StableAtmosphereState& stable_state,
    internal::AtmosphereLutCache& cache) -> RecordState;

private:
  struct alignas(16) PassConstants {
    std::uint32_t output_buffer_uav { 0U };
    std::uint32_t transmittance_lut_srv { 0U };
    std::uint32_t multi_scattering_lut_srv { 0U };
    std::uint32_t transmittance_width { 0U };
    std::uint32_t transmittance_height { 0U };
    std::uint32_t multi_scattering_width { 0U };
    std::uint32_t multi_scattering_height { 0U };
    std::uint32_t active_light_count { 0U };
    std::uint32_t integration_sample_count { 64U };
    float planet_radius_m { 6360000.0F };
    float atmosphere_height_m { 100000.0F };
    float sample_altitude_km { 1.0F };
    float multi_scattering_factor { 1.0F };
    float rayleigh_scale_height_m { 8000.0F };
    float mie_scale_height_m { 1200.0F };
    float mie_anisotropy { 0.8F };
    float _pad0 { 0.0F };
    float light0_direction_ws[4] { 0.0F, 0.0F, 1.0F, 0.0F };
    float light1_direction_ws[4] { 0.0F, 0.0F, 1.0F, 0.0F };
    float light0_illuminance_rgb[4] { 0.0F, 0.0F, 0.0F, 0.0F };
    float light1_illuminance_rgb[4] { 0.0F, 0.0F, 0.0F, 0.0F };
    float sky_luminance_factor_rgb[4] { 1.0F, 1.0F, 1.0F, 0.0F };
    float ground_albedo_rgb[4] { 0.1F, 0.1F, 0.1F, 0.0F };
    float rayleigh_scattering_rgb[4] { 5.8e-6F, 13.5e-6F, 33.1e-6F, 0.0F };
    float mie_scattering_rgb[4] { 2.0e-5F, 2.0e-5F, 2.0e-5F, 0.0F };
    float mie_absorption_rgb[4] { 4.4e-6F, 4.4e-6F, 4.4e-6F, 0.0F };
    float ozone_absorption_rgb[4] { 0.65e-6F, 1.88e-6F, 0.08e-6F, 0.0F };
    float ozone_density_layer0[4] { 25000.0F, 0.0F, 0.0F, 0.0F };
    float ozone_density_layer1[4] { 0.0F, 0.0F, 0.0F, 0.0F };
  };

  Renderer& renderer_;
  upload::TransientStructuredBuffer pass_constants_buffer_;
};

} // namespace environment
} // namespace oxygen::vortex
