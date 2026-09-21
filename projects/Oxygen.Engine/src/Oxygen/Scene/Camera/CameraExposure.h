//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <limits>

#include <Oxygen/Core/Types/PostProcess.h>

namespace oxygen::scene {

//! Camera exposure settings expressed as physical camera parameters.
/*!
 Stores exposure in terms of aperture, shutter rate, and ISO. This structure
 provides helpers to derive exposure values in EV (EV100, ISO 100 reference).

 ### Usage Patterns

  - Author the exposure settings on a camera component.
  - Convert to EV when building exposure values for rendering.

 @see PerspectiveCamera, OrthographicCamera
*/
struct CameraExposure {
  //! Aperture as f-number (f/stop).
  float aperture_f = engine::kDefaultCameraApertureF;

  //! Shutter rate in 1/seconds (e.g. 125 for 1/125 s).
  float shutter_rate = engine::kDefaultCameraShutterRate;

  //! Sensor ISO sensitivity (e.g. 100, 400).
  float iso = engine::kDefaultCameraIso;

  //! Computes EV (EV100, ISO 100 reference) for the current exposure settings.
  /*!
   @return EV100, or NaN for nonfinite/nonpositive physical camera inputs.

  ### Performance Characteristics

  - Time Complexity: $O(1)$
  - Memory: $O(1)$
  - Optimization: None
  */
  [[nodiscard]] auto GetEv() const noexcept -> float
  {
    if (!std::isfinite(aperture_f) || !std::isfinite(shutter_rate)
      || !std::isfinite(iso) || aperture_f <= 0.0F || shutter_rate <= 0.0F
      || iso <= 0.0F) {
      return std::numeric_limits<float>::quiet_NaN();
    }
    return static_cast<float>(2.0 * std::log2(static_cast<double>(aperture_f))
      + std::log2(static_cast<double>(shutter_rate))
      - std::log2(static_cast<double>(iso) / 100.0));
  }
};

} // namespace oxygen::scene
