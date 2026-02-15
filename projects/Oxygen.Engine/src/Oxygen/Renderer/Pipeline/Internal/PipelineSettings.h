//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Renderer/Passes/AutoExposurePass.h>
#include <Oxygen/Renderer/Passes/GroundGridPass.h>
#include <Oxygen/Renderer/Pipeline/RenderMode.h>
#include <Oxygen/Renderer/Types/ShaderDebugMode.h>

namespace oxygen::renderer::internal {

struct PipelineSettings {
  engine::ShaderDebugMode shader_debug_mode {
    engine::ShaderDebugMode::kDisabled
  };
  RenderMode render_mode { RenderMode::kSolid };
  graphics::Color wire_color { 1.0F, 1.0F, 1.0F, 1.0F };
  engine::ShaderDebugMode light_culling_debug_mode {
    engine::ShaderDebugMode::kDisabled
  };
  std::uint32_t cluster_depth_slices { 24 };
  engine::ExposureMode exposure_mode { engine::ExposureMode::kManual };
  float exposure_value { 1.0F };
  engine::ToneMapper tonemapping_mode { engine::ToneMapper::kAcesFitted };
  float gamma { 2.2F };
  engine::GroundGridPassConfig ground_grid_config {};
  float auto_exposure_adaptation_speed_up {
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedUp
  };
  float auto_exposure_adaptation_speed_down {
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedDown
  };
  float auto_exposure_low_percentile {
    engine::AutoExposurePassConfig::kDefaultLowPercentile
  };
  float auto_exposure_high_percentile {
    engine::AutoExposurePassConfig::kDefaultHighPercentile
  };
  float auto_exposure_min_log_luminance {
    engine::AutoExposurePassConfig::kDefaultMinLogLuminance
  };
  float auto_exposure_log_luminance_range {
    engine::AutoExposurePassConfig::kDefaultLogLuminanceRange
  };
  float auto_exposure_target_luminance {
    engine::AutoExposurePassConfig::kDefaultTargetLuminance
  };
  float auto_exposure_spot_meter_radius {
    engine::AutoExposurePassConfig::kDefaultSpotMeterRadius
  };
  engine::MeteringMode auto_exposure_metering {
    engine::AutoExposurePassConfig::kDefaultMeteringMode
  };
  bool gpu_debug_pass_enabled { true };
  bool atmosphere_blue_noise_enabled { true };
  std::optional<SubPixelPosition> gpu_debug_mouse_down_position;
};

struct PipelineSettingsDraft : PipelineSettings {
  bool auto_exposure_reset_pending { false };
  float auto_exposure_reset_ev { 0.0F };
  bool dirty { true };

  struct CommitResult {
    PipelineSettings settings;
    std::optional<float> auto_exposure_reset_ev;
  };

  auto Commit() -> CommitResult;
};

} // namespace oxygen::renderer::internal
