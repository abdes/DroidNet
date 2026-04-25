//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Types/PostProcess.h>

namespace oxygen::vortex {

struct PostProcessConfig {
  engine::ToneMapper tone_mapper { engine::ToneMapper::kAcesFitted };
  engine::MeteringMode metering_mode { engine::MeteringMode::kAverage };
  bool enable_bloom { true };
  bool enable_auto_exposure { true };
  float fixed_exposure { 1.0F };
  float gamma { 2.2F };
  float bloom_intensity { 0.5F };
  float bloom_threshold { 1.0F };
  float auto_exposure_speed_up { 3.0F };
  float auto_exposure_speed_down { 3.0F };
  float auto_exposure_low_percentile { 0.1F };
  float auto_exposure_high_percentile { 0.9F };
  float auto_exposure_min_ev { -6.0F };
  float auto_exposure_max_ev { 16.0F };
  float auto_exposure_min_log_luminance { -12.0F };
  float auto_exposure_log_luminance_range { 25.0F };
  float auto_exposure_target_luminance { 0.18F };
  float auto_exposure_spot_meter_radius { 0.2F };
};

} // namespace oxygen::vortex
