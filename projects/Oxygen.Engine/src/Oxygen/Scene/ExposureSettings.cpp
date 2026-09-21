//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::scene {
namespace {

  constexpr double kMinLogGain = -32.0;
  constexpr double kMaxLogGain = 32.0;
  constexpr double kMiddleGrey = engine::kExposureMiddleGrey;

  // Preserve small terms when large authored compensation values cancel.
  auto AccurateSum(const std::initializer_list<double> values) -> double
  {
    double sum = 0.0;
    double correction = 0.0;
    for (const double value : values) {
      const double next = sum + value;
      correction += std::abs(sum) >= std::abs(value) ? (sum - next) + value
                                                     : (value - next) + sum;
      sum = next;
    }
    return sum + correction;
  }

  auto SupportedGain(const double log_gain) noexcept -> bool
  {
    return std::isfinite(log_gain) && log_gain >= kMinLogGain
      && log_gain <= kMaxLogGain;
  }

  auto CurveValue(const ExposureSettings& settings, const double ev) -> double
  {
    const auto& keys = settings.compensation_curve;
    if (keys.empty()) {
      return 0.0;
    }
    if (ev <= keys.front().metered_ev) {
      return keys.front().compensation_ev;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
      if (ev <= keys[i].metered_ev) {
        const double left = keys[i - 1].metered_ev;
        const double right = keys[i].metered_ev;
        const double alpha = (ev - left) / (right - left);
        return std::lerp(static_cast<double>(keys[i - 1].compensation_ev),
          static_cast<double>(keys[i].compensation_ev), alpha);
      }
    }
    return keys.back().compensation_ev;
  }

} // namespace

auto ResolveExposureSettings(
  const ExposureSettings& settings, const std::optional<float> camera_ev)
  -> Result<ResolvedExposureSettings, ExposureSettingsError>
{
  using Error = ExposureSettingsError;
  if (settings.mode != engine::ExposureMode::kManual
    && settings.mode != engine::ExposureMode::kManualCamera
    && settings.mode != engine::ExposureMode::kAuto) {
    return ::oxygen::Err(Error::kUnknownMode);
  }
  const auto scalars
    = std::array { settings.manual_ev, settings.compensation_ev, settings.key,
        settings.min_ev, settings.max_ev, settings.speed_up,
        settings.speed_down, settings.low_percentile, settings.high_percentile,
        settings.min_log_luminance, settings.log_luminance_range,
        settings.target_luminance, settings.spot_meter_radius,
        settings.black_influence, settings.transition_distance };
  if (!std::ranges::all_of(
        scalars, [](const float value) { return std::isfinite(value); })) {
    return ::oxygen::Err(Error::kNonFinite);
  }
  if (settings.key <= 0.0F) {
    return ::oxygen::Err(Error::kNonPositiveKey);
  }
  if (settings.min_ev > settings.max_ev) {
    return ::oxygen::Err(Error::kInvalidEvRange);
  }
  const double window_min = settings.min_log_luminance;
  const double window_max = window_min + settings.log_luminance_range;
  if (settings.log_luminance_range <= 0.0F
    || !std::isfinite(1.0F / settings.log_luminance_range)
    || window_min < engine::kMinExposureLogLuminance
    || window_max > engine::kMaxExposureLogLuminance) {
    return ::oxygen::Err(Error::kInvalidHistogramWindow);
  }
  if (settings.low_percentile < 0.0F || settings.high_percentile > 1.0F
    || settings.low_percentile >= settings.high_percentile) {
    return ::oxygen::Err(Error::kInvalidPercentiles);
  }
  if (settings.speed_up < 0.0F || settings.speed_down < 0.0F
    || settings.target_luminance < 0.0F) {
    return ::oxygen::Err(Error::kNegativeRateOrTarget);
  }
  if ((settings.metering_mode != engine::MeteringMode::kAverage
        && settings.metering_mode != engine::MeteringMode::kCenterWeighted
        && settings.metering_mode != engine::MeteringMode::kSpot)
    || settings.spot_meter_radius < 0.0F || settings.black_influence < 0.0F
    || settings.black_influence > 1.0F) {
    return ::oxygen::Err(Error::kInvalidProfile);
  }
  if (settings.transition_distance <= 0.0F) {
    return ::oxygen::Err(Error::kInvalidTransitionDistance);
  }
  if (settings.compensation_curve.size()
    > engine::kMaxExposureCompensationCurveKeys) {
    return ::oxygen::Err(Error::kInvalidCurve);
  }
  double previous_ev = -std::numeric_limits<double>::infinity();
  for (const auto& key : settings.compensation_curve) {
    if (!std::isfinite(key.metered_ev) || !std::isfinite(key.compensation_ev)
      || key.metered_ev <= previous_ev) {
      return ::oxygen::Err(Error::kInvalidCurve);
    }
    previous_ev = key.metered_ev;
  }

  const double log_key = std::log2(static_cast<double>(settings.key) / 12.5);
  auto result = ResolvedExposureSettings {
    .authored = settings,
  };
  if (!settings.enabled) {
    return ::oxygen::Ok(std::move(result));
  }
  if (settings.mode != engine::ExposureMode::kAuto) {
    if (settings.mode == engine::ExposureMode::kManualCamera
      && !camera_ev.has_value()) {
      return ::oxygen::Err(Error::kMissingCameraEv);
    }
    const double ev = settings.mode == engine::ExposureMode::kManualCamera
      ? *camera_ev
      : settings.manual_ev;
    // Subtract the authored EV before adding the key term, retaining
    // cancellation without overflowing an intermediate exp2(compensation) or
    // camera product.
    const double log_gain
      = (static_cast<double>(settings.compensation_ev) - ev) + log_key;
    if (!SupportedGain(log_gain)) {
      return ::oxygen::Err(Error::kUnsupportedGain);
    }
    result.fixed_scale = static_cast<float>(std::exp2(log_gain));
    return ::oxygen::Ok(std::move(result));
  }

  const auto target_at = [&](const double raw_ev) {
    const double bounded_ev
      = std::clamp(raw_ev, static_cast<double>(settings.min_ev),
        static_cast<double>(settings.max_ev));
    return AccurateSum({ settings.compensation_ev, CurveValue(settings, raw_ev),
      -bounded_ev, log_key,
      settings.target_luminance > 0.0F
        ? std::log2(
            static_cast<double>(settings.target_luminance) / kMiddleGrey)
        : 0.0 });
  };
  const auto valid_target
    = [&](const double ev) { return SupportedGain(target_at(ev)); };
  const double initial_ev = std::clamp(0.0F, settings.min_ev, settings.max_ev);
  if (!valid_target(settings.min_ev) || !valid_target(initial_ev)) {
    return ::oxygen::Err(Error::kUnsupportedGain);
  }
  result.initial_log_gain = static_cast<float>(target_at(initial_ev));
  result.dark_log_gain = static_cast<float>(target_at(settings.min_ev));
  if (settings.min_ev == settings.max_ev) {
    if (!valid_target(settings.min_ev)) {
      return ::oxygen::Err(Error::kUnsupportedGain);
    }
    result.auto_log_targets.push_back(
      { settings.min_ev, result.dark_log_gain });
    return ::oxygen::Ok(std::move(result));
  }
  const double raw_min
    = engine::kMinExposureLogLuminance - std::log2(kMiddleGrey);
  const double raw_max
    = engine::kMaxExposureLogLuminance - std::log2(kMiddleGrey);
  // Extrema of curve(raw_ev)-clamp(raw_ev) occur only at these breakpoints.
  if (!valid_target(raw_min) || !valid_target(raw_max)
    || !valid_target(settings.min_ev)
    || !valid_target(std::clamp(0.0F, settings.min_ev, settings.max_ev))) {
    return ::oxygen::Err(Error::kUnsupportedGain);
  }
  for (const double ev : { static_cast<double>(settings.min_ev),
         static_cast<double>(settings.max_ev) }) {
    if (ev > raw_min && ev < raw_max && !valid_target(ev)) {
      return ::oxygen::Err(Error::kUnsupportedGain);
    }
  }
  for (const auto& key : settings.compensation_curve) {
    if (key.metered_ev > raw_min && key.metered_ev < raw_max
      && !valid_target(key.metered_ev)) {
      return ::oxygen::Err(Error::kUnsupportedGain);
    }
  }
  // Normalize the complete target function before float32 GPU consumption.
  // Its breakpoints are the authored knots, clamp edges and supported radiance
  // endpoints. Last-valid metering survives changes to the histogram window.
  auto positions = std::vector<float> { static_cast<float>(raw_min),
    static_cast<float>(raw_max) };
  for (const float ev : { settings.min_ev, settings.max_ev }) {
    if (ev > raw_min && ev < raw_max) {
      positions.push_back(ev);
    }
  }
  for (const auto& key : settings.compensation_curve) {
    if (key.metered_ev > raw_min && key.metered_ev < raw_max) {
      positions.push_back(key.metered_ev);
    }
  }
  std::ranges::sort(positions);
  positions.erase(
    std::unique(positions.begin(), positions.end()), positions.end());
  for (const float ev : positions) {
    const double gain = target_at(ev);
    if (!SupportedGain(gain)) {
      return ::oxygen::Err(Error::kUnsupportedGain);
    }
    result.auto_log_targets.push_back({ ev, static_cast<float>(gain) });
  }
  return ::oxygen::Ok(std::move(result));
}

auto ResolveExposureSeedLogGain(const ResolvedExposureSettings& resolved,
  const float seed_ev) -> Result<float, ExposureSettingsError>
{
  const auto& settings = resolved.authored;
  if (!settings.enabled || settings.mode != engine::ExposureMode::kAuto) {
    return ::oxygen::Err(ExposureSettingsError::kSeedRequiresAuto);
  }
  if (!std::isfinite(seed_ev)) {
    return ::oxygen::Err(ExposureSettingsError::kNonFinite);
  }
  // Explicit seeds are not metered targets: do not clamp their EV or form
  // 0.18*exp2(EV). Opposing authored terms must cancel before float upload.
  const double log_gain = AccurateSum({ settings.compensation_ev,
    CurveValue(settings, seed_ev), -static_cast<double>(seed_ev),
    std::log2(static_cast<double>(settings.key) / 12.5),
    settings.target_luminance > 0.0F
      ? std::log2(static_cast<double>(settings.target_luminance) / kMiddleGrey)
      : 0.0 });
  if (!SupportedGain(log_gain)) {
    return ::oxygen::Err(ExposureSettingsError::kUnsupportedGain);
  }
  return ::oxygen::Ok(static_cast<float>(log_gain));
}

auto to_string(const ExposureSettingsError error) noexcept -> std::string_view
{
  switch (error) {
  case ExposureSettingsError::kUnknownMode:
    return "unknown exposure mode";
  case ExposureSettingsError::kNonFinite:
    return "nonfinite exposure field";
  case ExposureSettingsError::kNonPositiveKey:
    return "exposure key must be positive";
  case ExposureSettingsError::kInvalidEvRange:
    return "unordered exposure EV range";
  case ExposureSettingsError::kInvalidHistogramWindow:
    return "histogram window outside supported radiance domain";
  case ExposureSettingsError::kInvalidPercentiles:
    return "invalid exposure percentiles";
  case ExposureSettingsError::kNegativeRateOrTarget:
    return "negative exposure rate or target";
  case ExposureSettingsError::kInvalidProfile:
    return "invalid exposure metering profile";
  case ExposureSettingsError::kInvalidTransitionDistance:
    return "exposure transition distance must be positive";
  case ExposureSettingsError::kInvalidCurve:
    return "invalid exposure compensation curve";
  case ExposureSettingsError::kMissingCameraEv:
    return "ManualCamera requires camera EV100";
  case ExposureSettingsError::kUnsupportedGain:
    return "exposure gain outside supported numerical domain";
  case ExposureSettingsError::kSeedRequiresAuto:
    return "exposure seeds require enabled Auto mode";
  }
  return "unknown exposure validation error";
}

} // namespace oxygen::scene
