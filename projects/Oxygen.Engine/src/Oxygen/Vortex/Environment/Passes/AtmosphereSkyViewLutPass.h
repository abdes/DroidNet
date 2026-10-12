//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
namespace internal {
  class RetainedTexturePool;
}
namespace testing {
  struct RendererPublicationProbe;
}

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
      //! Submitted radiance resource retained for this view's consumers.
      std::shared_ptr<const graphics::Texture> texture;
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
    OXGN_VRTX_API auto RemoveViewState(ViewId view_id) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache) -> RecordState;

    //! Writes a scene-linear LUT to caller-owned managed storage at the
    //! supplied capture referential, independent of the current camera/view.
    [[nodiscard]] OXGN_VRTX_API auto RecordCapture(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache,
      const std::shared_ptr<graphics::Texture>& target,
      const graphics::RegistrationLease& registration) -> RecordState;

  private:
    auto RecordToTarget(RenderContext& ctx, graphics::CommandRecorder& recorder,
      const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache,
      const std::shared_ptr<graphics::Texture>& texture,
      ShaderVisibleIndex sky_view_srv, ShaderVisibleIndex sky_view_uav,
      bool capture) -> RecordState;
    friend struct ::oxygen::vortex::testing::RendererPublicationProbe;
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
      std::uint32_t reserved0 { 0U };
      std::uint32_t exposure_fp16_store { 0U };
    };

    struct alignas(16) SamplingAtmosphere0 {
      float sample_count_min { 0.0F };
      float sample_count_max { 0.0F };
      float distance_to_sample_count_max_inv { 0.0F };
      float planet_radius_km { 0.0F };
    };

    struct alignas(16) SamplingAtmosphere1 {
      float atmosphere_height_km { 0.0F };
      float camera_altitude_km { 0.0F };
      float rayleigh_scale_height_km { 0.0F };
      float mie_scale_height_km { 0.0F };
    };

    struct alignas(16) PhaseFactors0 {
      float multi_scattering_factor { 0.0F };
      float mie_anisotropy { 0.0F };
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
      std::array<float, 4> ground_albedo_rgb {};
      std::array<float, 4> rayleigh_scattering_per_km_rgb {};
      std::array<float, 4> mie_scattering_per_km_rgb {};
      std::array<float, 4> mie_absorption_per_km_rgb {};
      std::array<float, 4> ozone_absorption_per_km_rgb {};
      std::array<float, 4> ozone_density_layer0 {};
      std::array<float, 4> ozone_density_layer1 {};
      std::array<float, 4> sky_view_lut_referential_row0 {};
      std::array<float, 4> sky_view_lut_referential_row1 {};
      std::array<float, 4> sky_view_lut_referential_row2 {};
      std::array<float, 4> sky_luminance_factor_rgb {};
      std::array<float, 4> sky_and_aerial_luminance_factor_rgb {};
      std::array<float, 4> light0_direction_enabled {};
      std::array<float, 4> light0_illuminance_rgb {};
      std::array<float, 4> light1_direction_enabled {};
      std::array<float, 4> light1_illuminance_rgb {};
    };

    Renderer& renderer_;
    std::shared_ptr<upload::TransientStructuredBuffer> pass_constants_buffer_;
    std::unique_ptr<::oxygen::vortex::internal::RetainedTexturePool>
      output_pool_;
    std::vector<std::shared_ptr<graphics::Texture>> live_textures_;
  };

} // namespace environment
} // namespace oxygen::vortex
