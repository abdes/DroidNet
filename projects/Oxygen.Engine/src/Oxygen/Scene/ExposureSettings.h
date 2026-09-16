//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

//! One authored control point in the automatic exposure compensation curve.
struct ExposureCompensationKey {
  float metered_ev { 0.0F };
  float compensation_ev { 0.0F };

  auto operator==(const ExposureCompensationKey&) const -> bool = default;
};

//! Canonical exposure intent shared by scene authoring and per-view overrides.
/*!
 Values are requested settings, never GPU adaptation history. Resolve the entire
 value atomically before activating it. Defaults are ordinary scene defaults;
 a client such as DemoShell may explicitly choose a different initial mode.
*/
struct ExposureSettings {
  bool enabled { true };
  engine::ExposureMode mode { engine::ExposureMode::kAuto };
  float manual_ev { 9.7F };
  float compensation_ev { 0.0F };
  float key { 10.0F };
  float min_ev { -6.0F };
  float max_ev { 16.0F };
  float speed_up { 3.0F };
  float speed_down { 1.0F };
  engine::MeteringMode metering_mode { engine::MeteringMode::kAverage };
  float low_percentile { 0.1F };
  float high_percentile { 0.9F };
  float min_log_luminance { -12.0F };
  float log_luminance_range { 25.0F };
  float target_luminance { 0.18F };
  float spot_meter_radius { 0.2F };
  float black_influence { 0.0F };
  float transition_distance { 1.5F };
  data::AssetKey metering_mask {};
  std::vector<ExposureCompensationKey> compensation_curve {};

  auto operator==(const ExposureSettings&) const -> bool = default;
};

//! A normalized automatic target evaluated using raw metered EV100.
struct ExposureLogTargetKey {
  float metered_ev { 0.0F };
  float log_gain { 0.0F };
};

//! Validation failure for an entire exposure-settings revision.
enum class ExposureSettingsError : std::uint8_t {
  kUnknownMode,
  kNonFinite,
  kNonPositiveKey,
  kInvalidEvRange,
  kInvalidHistogramWindow,
  kInvalidPercentiles,
  kNegativeRateOrTarget,
  kInvalidProfile,
  kInvalidTransitionDistance,
  kInvalidCurve,
  kMissingCameraEv,
  kUnsupportedGain,
};

//! Validated authored settings and constants, with no numerical Auto history.
struct ResolvedExposureSettings {
  ExposureSettings authored;
  float fixed_scale { 1.0F };
  float initial_log_gain { 0.0F };
  float dark_log_gain { 0.0F };
  std::vector<ExposureLogTargetKey> auto_log_targets;
};

//! Validate without clamping or partially applying a settings revision.
/*!
 Camera EV is required only for enabled ManualCamera mode. Automatic gain bounds
 are checked at every piecewise-linear extremum, including histogram endpoints,
 EV bounds, curve keys, dark solve and initial fallback. Mask residency is an
 asynchronous resource-owner concern after numerical validation succeeds.
*/
[[nodiscard]] OXGN_SCN_API auto ResolveExposureSettings(
  const ExposureSettings& settings, std::optional<float> camera_ev = {})
  -> std::expected<ResolvedExposureSettings, ExposureSettingsError>;

[[nodiscard]] OXGN_SCN_API auto to_string(ExposureSettingsError error) noexcept
  -> std::string_view;

} // namespace oxygen::scene
