//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Core/api_export.h>

namespace oxygen::engine {

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
