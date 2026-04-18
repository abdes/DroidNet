//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include <Oxygen/Core/api_export.h>

namespace oxygen::engine {

inline constexpr float kExposureCalibrationKey = 12.5F;
inline constexpr float kExposureMiddleGrey = 0.18F;

[[nodiscard]] inline auto ExposureBiasScale(
  const float compensation_ev, const float exposure_key) noexcept -> float
{
  return std::exp2(compensation_ev) * (exposure_key / kExposureCalibrationKey);
}

[[nodiscard]] inline auto Ev100ToUnitlessLuminance(const float ev100) noexcept
  -> float
{
  return std::exp2(ev100);
}

[[nodiscard]] inline auto Ev100ToAverageLuminance(const float ev100) noexcept
  -> float
{
  return kExposureMiddleGrey * Ev100ToUnitlessLuminance(ev100);
}

[[nodiscard]] inline auto AverageLuminanceToEv100(
  const float average_luminance) noexcept -> float
{
  return std::log2(average_luminance / kExposureMiddleGrey);
}

[[nodiscard]] inline auto ExposureScaleFromEv100(const float ev100,
  const float compensation_ev, const float exposure_key) noexcept -> float
{
  return ExposureBiasScale(compensation_ev, exposure_key)
    / Ev100ToUnitlessLuminance(ev100);
}

//! Metering modes for auto exposure.
// NOLINTNEXTLINE(performance-enum-size)
enum class MeteringMode : std::uint32_t {
  kAverage = 0U,
  kCenterWeighted = 1U,
  kSpot = 2U,
};

//! Returns a string representation of the metering mode.
OXGN_CORE_NDAPI auto to_string(MeteringMode mode) -> std::string_view;

//! Standardized exposure modes for rendering.
enum class ExposureMode : std::uint32_t { // NOLINT(*-enum-size)
  kManual = 0,
  kManualCamera = 1,
  kAuto = 2,
};

//! Returns a string representation of the exposure mode.
OXGN_CORE_NDAPI auto to_string(ExposureMode mode) -> std::string_view;

//! Standardized tonemapper selection.
enum class ToneMapper : std::uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kAcesFitted = 1,
  kFilmic = 2,
  kReinhard = 3,
};

//! Returns a string representation of the tonemapper.
OXGN_CORE_NDAPI auto to_string(ToneMapper mapper) -> std::string_view;

} // namespace oxygen::engine
