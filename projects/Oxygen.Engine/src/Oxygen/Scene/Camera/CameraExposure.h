//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>

namespace oxygen::scene {

//! Camera exposure settings expressed as physical camera parameters.
/*!
 Stores exposure in terms of aperture, shutter rate, and ISO. This structure
 provides helpers to derive EV100 values for exposure computation.

 ### Usage Patterns

  - Author the exposure settings on a camera component.
  - Convert to EV100 when building exposure values for rendering.

 @see PerspectiveCamera, OrthographicCamera
*/
struct CameraExposure {
  //! Aperture as f-number (f/stop).
  float aperture_f = 11.0F;

  //! Shutter rate in 1/seconds (e.g. 125 for 1/125 s).
  float shutter_rate = 125.0F;

  //! Sensor ISO sensitivity (e.g. 100, 400).
  float iso = 100.0F;

  //! Computes EV100 for the current exposure settings.
  /*!
   @return EV100 for this exposure configuration.

  ### Performance Characteristics

  - Time Complexity: $O(1)$
  - Memory: $O(1)$
  - Optimization: None
  */
  [[nodiscard]] auto GetEv100() const noexcept -> float
  {
    const float safe_aperture = std::max(aperture_f, 0.1F);
    const float safe_shutter_rate = std::max(shutter_rate, 0.001F);
    const float safe_iso = std::max(iso, 1.0F);
    const float t = 1.0F / safe_shutter_rate;
    const float ev100 = std::log2((safe_aperture * safe_aperture) / t)
      - std::log2(safe_iso / 100.0F);
    return ev100;
  }
};

} // namespace oxygen::scene
