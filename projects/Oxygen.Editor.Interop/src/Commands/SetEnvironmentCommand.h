//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  struct SkyAtmosphereParams {
    bool enabled = true;
    bool sun_disk_enabled = true;
    float planet_radius_m = 6'360'000.0F;
    float atmosphere_height_m = 80'000.0F;
    Vec3 ground_albedo_rgb { 0.4F, 0.4F, 0.4F };
    float rayleigh_scale_height_m = 8'000.0F;
    float mie_scale_height_m = 1'200.0F;
    float mie_anisotropy = 0.8F;
    Vec3 sky_luminance_factor_rgb { 1.0F, 1.0F, 1.0F };
    float aerial_perspective_distance_scale = 1.0F;
    float aerial_scattering_strength = 1.0F;
    float aerial_perspective_start_depth_m = 0.0F;
    float height_fog_contribution = 1.0F;
  };

  struct PostProcessParams {
    int tone_mapper = 1;
    int exposure_mode = 2;
    bool exposure_enabled = true;
    float exposure_compensation_ev = 0.0F;
    float exposure_key = 10.0F;
    float manual_exposure_ev = 9.7F;
    float auto_exposure_min_ev = -6.0F;
    float auto_exposure_max_ev = 16.0F;
    float auto_exposure_speed_up = 3.0F;
    float auto_exposure_speed_down = 1.0F;
    int auto_exposure_metering_mode = 0;
    float auto_exposure_low_percentile = 0.1F;
    float auto_exposure_high_percentile = 0.9F;
    float auto_exposure_min_log_luminance = -12.0F;
    float auto_exposure_log_luminance_range = 25.0F;
    float auto_exposure_target_luminance = 0.18F;
    float auto_exposure_spot_meter_radius = 0.2F;
    float bloom_intensity = 0.0F;
    float bloom_threshold = 1.0F;
    float saturation = 1.0F;
    float contrast = 1.0F;
    float vignette_intensity = 0.0F;
    float display_gamma = 2.2F;
  };

  class SetEnvironmentCommand final : public EditorCommand {
  public:
    SetEnvironmentCommand(
      SkyAtmosphereParams atmosphere, PostProcessParams post_process)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , atmosphere_(atmosphere)
      , post_process_(post_process)
    {
    }

    void Execute(CommandContext& context) override;

  private:
    SkyAtmosphereParams atmosphere_;
    PostProcessParams post_process_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
