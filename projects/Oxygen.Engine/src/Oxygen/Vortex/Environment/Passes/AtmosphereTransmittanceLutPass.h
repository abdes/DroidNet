//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Types/Atmosphere.h>
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

class AtmosphereTransmittanceLutPass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    ShaderVisibleIndex transmittance_lut_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex transmittance_lut_uav { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t dispatch_count_x { 0U };
    std::uint32_t dispatch_count_y { 0U };
    std::uint32_t dispatch_count_z { 0U };
  };

  OXGN_VRTX_API explicit AtmosphereTransmittanceLutPass(Renderer& renderer);
  OXGN_VRTX_API ~AtmosphereTransmittanceLutPass();

  AtmosphereTransmittanceLutPass(const AtmosphereTransmittanceLutPass&) = delete;
  auto operator=(const AtmosphereTransmittanceLutPass&)
    -> AtmosphereTransmittanceLutPass& = delete;
  AtmosphereTransmittanceLutPass(AtmosphereTransmittanceLutPass&&) = delete;
  auto operator=(AtmosphereTransmittanceLutPass&&)
    -> AtmosphereTransmittanceLutPass& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    const internal::StableAtmosphereState& stable_state,
    internal::AtmosphereLutCache& cache) -> RecordState;

private:
  struct alignas(16) PassConstants {
    std::uint32_t output_texture_uav { 0U };
    std::uint32_t output_width { 0U };
    std::uint32_t output_height { 0U };
    std::uint32_t integration_sample_count { 0U };
    float planet_radius_km { 6360.0F };
    float atmosphere_height_km { 100.0F };
    float rayleigh_scale_height_km { 8.0F };
    float mie_scale_height_km { 1.2F };
    float rayleigh_scattering_per_km_rgb[4] { 5.8e-3F, 13.5e-3F, 33.1e-3F,
      0.0F };
    float mie_scattering_per_km_rgb[4] { 2.0e-2F, 2.0e-2F, 2.0e-2F, 0.0F };
    float mie_absorption_per_km_rgb[4] { 4.4e-3F, 4.4e-3F, 4.4e-3F, 0.0F };
    float ozone_absorption_per_km_rgb[4] { 0.65e-3F, 1.88e-3F, 0.08e-3F,
      0.0F };
    float ozone_density_layer0[4] { 25.0F, 0.0F, 0.0F, 0.0F };
    float ozone_density_layer1[4] { 0.0F, 0.0F, 0.0F, 0.0F };
  };

  Renderer& renderer_;
  upload::TransientStructuredBuffer pass_constants_buffer_;
};

} // namespace environment
} // namespace oxygen::vortex
