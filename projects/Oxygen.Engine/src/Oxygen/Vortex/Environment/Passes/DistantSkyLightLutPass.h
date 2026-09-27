//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
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
      ShaderVisibleIndex distant_sky_light_lut_srv {
        kInvalidShaderVisibleIndex
      };
      ShaderVisibleIndex distant_sky_light_lut_uav {
        kInvalidShaderVisibleIndex
      };
      std::uint32_t dispatch_count_x { 0U };
      std::uint32_t dispatch_count_y { 0U };
      std::uint32_t dispatch_count_z { 0U };
    };

    OXGN_VRTX_API explicit DistantSkyLightLutPass(Renderer& renderer);
    OXGN_VRTX_API ~DistantSkyLightLutPass();

    OXYGEN_MAKE_NON_COPYABLE(DistantSkyLightLutPass)
    OXYGEN_MAKE_NON_MOVABLE(DistantSkyLightLutPass)

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    OXGN_VRTX_NDAPI auto Record(RenderContext& ctx,
      const internal::StableAtmosphereState& stable_state,
      internal::AtmosphereLutCache& cache, bool capture = false) -> RecordState;

  private:
    struct alignas(16) PassConstants {
      std::uint32_t output_buffer_uav { 0U };
      std::uint32_t transmittance_lut_srv { 0U };
      std::uint32_t multi_scattering_lut_srv { 0U };
      std::uint32_t transmittance_width { 0U };
      std::uint32_t transmittance_height { 0U };
      std::uint32_t multi_scattering_width { 0U };
      std::uint32_t multi_scattering_height { 0U };
      std::uint32_t light0_enabled { 0U };
      std::uint32_t light1_enabled { 0U };
      float planet_radius_km {};
      float atmosphere_height_km {};
      float sample_altitude_km {};
      float multi_scattering_factor {};
      float rayleigh_scale_height_km {};
      float mie_scale_height_km {};
      float mie_anisotropy {};
      float _pad0 {};
      float _pad1 {};
      float _pad2 {};
      float _pad3 {};
      std::array<float, 4> light0_direction_ws {};
      std::array<float, 4> light1_direction_ws {};
      std::array<float, 4> light0_illuminance_rgb {};
      std::array<float, 4> light1_illuminance_rgb {};
      std::array<float, 4> sky_luminance_factor_rgb {};
      std::array<float, 4> ground_albedo_rgb {};
      std::array<float, 4> rayleigh_scattering_per_km_rgb {};
      std::array<float, 4> mie_scattering_per_km_rgb {};
      std::array<float, 4> mie_absorption_per_km_rgb {};
      std::array<float, 4> ozone_absorption_per_km_rgb {};
      std::array<float, 4> ozone_density_layer0 {};
      std::array<float, 4> ozone_density_layer1 {};
    };
    // Shader ABI size is an exact layout assertion, not a tuning parameter.
    // NOLINTNEXTLINE(readability-magic-numbers)
    static_assert(sizeof(PassConstants) == 272U);

    Renderer& renderer_;
    std::shared_ptr<upload::TransientStructuredBuffer> pass_constants_buffer_;
  };

} // namespace environment
} // namespace oxygen::vortex
