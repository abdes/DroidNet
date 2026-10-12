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

  class AtmosphereCameraAerialPerspectivePass {
  public:
    struct RecordState {
      bool requested { false };
      bool executed { false };
      //! Submitted radiance resource retained for this view's consumers.
      std::shared_ptr<const graphics::Texture> texture;
      ShaderVisibleIndex camera_aerial_perspective_srv {
        kInvalidShaderVisibleIndex
      };
      ShaderVisibleIndex camera_aerial_perspective_uav {
        kInvalidShaderVisibleIndex
      };
      std::uint32_t width { 0U };
      std::uint32_t height { 0U };
      std::uint32_t depth { 0U };
      std::uint32_t dispatch_count_x { 0U };
      std::uint32_t dispatch_count_y { 0U };
      std::uint32_t dispatch_count_z { 0U };
    };

    OXGN_VRTX_API explicit AtmosphereCameraAerialPerspectivePass(
      Renderer& renderer);
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
    OXGN_VRTX_API auto RemoveViewState(ViewId view_id) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache) -> RecordState;

  private:
    friend struct ::oxygen::vortex::testing::RendererPublicationProbe;
    struct alignas(16) OutputHeader {
      std::uint32_t output_texture_uav { 0U };
      std::uint32_t output_width { 0U };
      std::uint32_t output_height { 0U };
      std::uint32_t output_depth { 0U };
    };

    struct alignas(16) LutHeader0 {
      std::uint32_t transmittance_lut_srv { 0U };
      std::uint32_t multi_scattering_lut_srv { 0U };
      std::uint32_t transmittance_width { 0U };
      std::uint32_t transmittance_height { 0U };
    };

    struct alignas(16) LutHeader1 {
      std::uint32_t multi_scattering_width { 0U };
      std::uint32_t multi_scattering_height { 0U };
      std::uint32_t active_light_count { 0U };
      std::uint32_t depth_resolution { 0U };
    };

    struct alignas(16) DepthControl0 {
      float fog_show_flag_factor { 0.0F };
      float real_time_reflection_360_mode { 0.0F };
      float depth_resolution_inv { 0.0F };
      float depth_slice_length_km { 0.0F };
    };

    struct alignas(16) DepthControl1 {
      float depth_slice_length_km_inv { 0.0F };
      float sample_count_per_slice { 0.0F };
      float start_depth_km { 0.0F };
      float planet_radius_km { 0.0F };
    };

    struct alignas(16) AtmosphereScales0 {
      float atmosphere_height_km { 0.0F };
      float aerial_perspective_distance_scale { 0.0F };
      std::uint32_t reserved0 { 0U };
      float rayleigh_scale_height_km { 0.0F };
    };

    struct alignas(16) AtmosphereScales1 {
      float mie_scale_height_km { 0.0F };
      float multi_scattering_factor { 0.0F };
      float mie_anisotropy { 0.0F };
      std::uint32_t exposure_fp16_store { 0U };
    };

    struct alignas(16) PassConstants {
      OutputHeader output_header {};
      LutHeader0 lut_header0 {};
      LutHeader1 lut_header1 {};
      DepthControl0 depth_control0 {};
      DepthControl1 depth_control1 {};
      AtmosphereScales0 atmosphere_scales0 {};
      AtmosphereScales1 atmosphere_scales1 {};
      std::array<float, 4> ground_albedo_rgb {};
      std::array<float, 4> rayleigh_scattering_per_km_rgb {};
      std::array<float, 4> mie_scattering_per_km_rgb {};
      std::array<float, 4> mie_absorption_per_km_rgb {};
      std::array<float, 4> ozone_absorption_per_km_rgb {};
      std::array<float, 4> ozone_density_layer0 {};
      std::array<float, 4> ozone_density_layer1 {};
      std::array<float, 4> camera_planet_position_km {};
      std::array<float, 4> sky_and_aerial_luminance_factor_rgb {};
      std::array<float, 4> light0_direction_enabled {};
      std::array<float, 4> light0_illuminance_rgb {};
      std::array<float, 4> light1_direction_enabled {};
      std::array<float, 4> light1_illuminance_rgb {};
    };

    Renderer& renderer_;
    upload::TransientStructuredBuffer pass_constants_buffer_;
    std::unique_ptr<::oxygen::vortex::internal::RetainedTexturePool>
      output_pool_;
    std::vector<std::shared_ptr<graphics::Texture>> live_textures_;
  };

} // namespace environment
} // namespace oxygen::vortex
