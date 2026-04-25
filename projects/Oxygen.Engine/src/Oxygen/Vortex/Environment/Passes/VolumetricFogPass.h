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
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
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

  class VolumetricFogPass {
  public:
    struct RecordState {
      bool requested { false };
      bool executed { false };
      ShaderVisibleIndex integrated_light_scattering_srv {
        kInvalidShaderVisibleIndex
      };
      ShaderVisibleIndex integrated_light_scattering_uav {
        kInvalidShaderVisibleIndex
      };
      std::uint32_t width { 0U };
      std::uint32_t height { 0U };
      std::uint32_t depth { 0U };
      std::uint32_t dispatch_count_x { 0U };
      std::uint32_t dispatch_count_y { 0U };
      std::uint32_t dispatch_count_z { 0U };
      float start_distance_m { 0.0F };
      float end_distance_m { 0.0F };
      bool view_constants_bound { false };
      bool ue_log_depth_distribution { false };
      bool directional_shadowed_light_injection_requested { false };
      bool height_fog_media_requested { false };
      bool height_fog_media_executed { false };
      bool sky_light_injection_requested { false };
      bool sky_light_injection_executed { false };
      bool local_fog_injection_requested { false };
      bool local_fog_injection_executed { false };
      std::uint32_t local_fog_instance_count { 0U };
      float grid_z_params[3] { 0.0F, 1.0F, 1.0F };
    };

    OXGN_VRTX_API explicit VolumetricFogPass(Renderer& renderer);
    OXGN_VRTX_API ~VolumetricFogPass();

    VolumetricFogPass(const VolumetricFogPass&) = delete;
    auto operator=(const VolumetricFogPass&) -> VolumetricFogPass& = delete;
    VolumetricFogPass(VolumetricFogPass&&) = delete;
    auto operator=(VolumetricFogPass&&) -> VolumetricFogPass& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      const internal::StableAtmosphereState& stable_state,
      ShaderVisibleIndex distant_sky_light_lut_srv,
      const internal::LocalFogVolumeState::ViewProducts* local_fog_products
      = nullptr) -> RecordState;

  private:
    struct alignas(16) OutputHeader {
      std::uint32_t output_texture_uav { 0U };
      std::uint32_t output_width { 0U };
      std::uint32_t output_height { 0U };
      std::uint32_t output_depth { 0U };
    };

    struct alignas(16) GridControl {
      float start_distance_m { 0.0F };
      float end_distance_m { 1000.0F };
      float near_fade_in_distance_m { 0.0F };
      float global_extinction_scale { 0.0F };
    };

    struct alignas(16) MediaControl0 {
      float albedo_rgb[3] { 1.0F, 1.0F, 1.0F };
      float scattering_distribution { 0.0F };
    };

    struct alignas(16) GridZControl {
      float grid_z_params[3] { 0.0F, 1.0F, 1.0F };
      float shadowed_directional_light0_enabled { 0.0F };
    };

    struct alignas(16) MediaControl1 {
      float emissive_rgb[3] { 0.0F, 0.0F, 0.0F };
      float static_lighting_scattering_intensity { 1.0F };
    };

    struct alignas(16) LocalFogControl0 {
      std::uint32_t instance_buffer_slot { kInvalidShaderVisibleIndex.get() };
      std::uint32_t tile_data_texture_slot { kInvalidShaderVisibleIndex.get() };
      std::uint32_t instance_count { 0U };
      std::uint32_t enabled { 0U };
    };

    struct alignas(16) LocalFogControl1 {
      std::uint32_t tile_resolution_x { 0U };
      std::uint32_t tile_resolution_y { 0U };
      std::uint32_t max_instances_per_tile { 0U };
      float global_start_distance_m { 0.0F };
    };

    struct alignas(16) LocalFogControl2 {
      float max_density_into_volumetric_fog { 0.0F };
      float _pad0 { 0.0F };
      float _pad1 { 0.0F };
      float _pad2 { 0.0F };
    };

    struct alignas(16) HeightFogMediaControl0 {
      float primary_density { 0.0F };
      float primary_height_falloff { 0.0F };
      float primary_height_offset_m { 0.0F };
      float secondary_density { 0.0F };
    };

    struct alignas(16) HeightFogMediaControl1 {
      float secondary_height_falloff { 0.0F };
      float secondary_height_offset_m { 0.0F };
      float match_height_fog_factor { 0.5F };
      std::uint32_t enabled { 0U };
    };

    struct alignas(16) SkyLightControl0 {
      std::uint32_t distant_sky_light_lut_slot {
        kInvalidShaderVisibleIndex.get()
      };
      std::uint32_t enabled { 0U };
      float volumetric_scattering_intensity { 0.0F };
      float diffuse_intensity { 0.0F };
    };

    struct alignas(16) SkyLightControl1 {
      float tint_rgb[3] { 1.0F, 1.0F, 1.0F };
      float intensity_mul { 1.0F };
    };

    struct alignas(16) PassConstants {
      OutputHeader output_header {};
      GridControl grid {};
      GridZControl grid_z {};
      MediaControl0 media0 {};
      MediaControl1 media1 {};
      HeightFogMediaControl0 height_fog0 {};
      HeightFogMediaControl1 height_fog1 {};
      SkyLightControl0 sky_light0 {};
      SkyLightControl1 sky_light1 {};
      LocalFogControl0 local_fog0 {};
      LocalFogControl1 local_fog1 {};
      LocalFogControl2 local_fog2 {};
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
