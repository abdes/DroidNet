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
  class AtmosphereLutCache;
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
    auto operator=(const AtmosphereSkyViewLutPass&)
      -> AtmosphereSkyViewLutPass& = delete;
    AtmosphereSkyViewLutPass(AtmosphereSkyViewLutPass&&) = delete;
    auto operator=(AtmosphereSkyViewLutPass&&)
      -> AtmosphereSkyViewLutPass& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache) -> RecordState;

  private:
    struct alignas(16) OutputHeader {
      std::uint32_t output_texture_uav { 0U };
      std::uint32_t output_width { 0U };
      std::uint32_t output_height { 0U };
      std::uint32_t transmittance_lut_srv { 0U };
    };

    struct alignas(16) LutHeader {
      std::uint32_t multi_scattering_lut_srv { 0U };
      std::uint32_t transmittance_width { 0U };
      std::uint32_t transmittance_height { 0U };
      std::uint32_t multi_scattering_width { 0U };
    };

    struct alignas(16) DispatchHeader {
      std::uint32_t multi_scattering_height { 0U };
      std::uint32_t active_light_count { 0U };
      std::uint32_t _pad0 { 0U };
      std::uint32_t _pad1 { 0U };
    };

    struct alignas(16) SamplingAtmosphere0 {
      float sample_count_min { 4.0F };
      float sample_count_max { 32.0F };
      float distance_to_sample_count_max_inv { 1.0F / 150000.0F };
      float planet_radius_m { 6360000.0F };
    };

    struct alignas(16) SamplingAtmosphere1 {
      float atmosphere_height_m { 100000.0F };
      float camera_altitude_m { 0.0F };
      float rayleigh_scale_height_m { 8000.0F };
      float mie_scale_height_m { 1200.0F };
    };

    struct alignas(16) PhaseFactors0 {
      float multi_scattering_factor { 1.0F };
      float mie_anisotropy { 0.8F };
      float _pad0 { 0.0F };
      float _pad1 { 0.0F };
    };

    struct alignas(16) PassConstants {
      OutputHeader output_header {};
      LutHeader lut_header {};
      DispatchHeader dispatch_header {};
      SamplingAtmosphere0 sampling_atmosphere0 {};
      SamplingAtmosphere1 sampling_atmosphere1 {};
      PhaseFactors0 phase_factors0 {};
      float ground_albedo_rgb[4] { 0.4F, 0.4F, 0.4F, 0.0F };
      float rayleigh_scattering_rgb[4] { 5.8e-6F, 13.5e-6F, 33.1e-6F, 0.0F };
      float mie_scattering_rgb[4] { 2.0e-5F, 2.0e-5F, 2.0e-5F, 0.0F };
      float mie_absorption_rgb[4] { 4.4e-6F, 4.4e-6F, 4.4e-6F, 0.0F };
      float ozone_absorption_rgb[4] { 0.65e-6F, 1.88e-6F, 0.08e-6F, 0.0F };
      float ozone_density_layer0[4] { 25000.0F, 0.0F, 0.0F, 0.0F };
      float ozone_density_layer1[4] { 0.0F, 0.0F, 0.0F, 0.0F };
      float sky_view_lut_referential_row0[4] { 1.0F, 0.0F, 0.0F, 0.0F };
      float sky_view_lut_referential_row1[4] { 0.0F, 1.0F, 0.0F, 0.0F };
      float sky_view_lut_referential_row2[4] { 0.0F, 0.0F, 1.0F, 0.0F };
      float sky_luminance_factor_rgb[4] { 1.0F, 1.0F, 1.0F, 0.0F };
      float sky_and_aerial_luminance_factor_rgb[4] { 1.0F, 1.0F, 1.0F, 0.0F };
      float light0_direction_enabled[4] { 0.0F, 0.0F, 1.0F, 0.0F };
      float light0_illuminance_rgb[4] { 0.0F, 0.0F, 0.0F, 0.0F };
      float light1_direction_enabled[4] { 0.0F, 0.0F, 1.0F, 0.0F };
      float light1_illuminance_rgb[4] { 0.0F, 0.0F, 0.0F, 0.0F };
    };

    Renderer& renderer_;
    upload::TransientStructuredBuffer pass_constants_buffer_;
    std::vector<std::shared_ptr<graphics::Texture>> live_textures_ {};
  };

} // namespace environment
} // namespace oxygen::vortex
