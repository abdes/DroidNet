//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::data {
class TextureResource;
} // namespace oxygen::data

namespace oxygen::vortex::resources {

//! Measures the illuminance a cube texture delivers on an upward-facing
//! surface, in lux.
/*!
 Texels are read as scene-linear radiance in cd/m^2. The measurement integrates
 their Rec. 709 luminance, weighted by the cosine to world up (+Z, the cube's
 +Y), over the upper hemisphere. It reads the smallest mip at least 32 texels
 wide; box-filtered mips preserve the integral, so a small sun still counts.

 @return The illuminance, or nothing for a texture that is not a single
   six-face cube in RGBA32Float, RGBA16Float, R11G11B10Float or R9G9B9E5Float
   (LDR and block-compressed cubes cannot be measured), or whose payload is
   malformed.
*/
OXGN_VRTX_NDAPI auto MeasureCubeIlluminance(
  const data::TextureResource& texture) -> std::optional<float>;

//! Scale that makes a source measured at `measured_lux` deliver
//! `target_lux`. A target of zero, or a source that delivers nothing, keeps the
//! radiance as imported (scale 1).
[[nodiscard]] constexpr auto CalibrationScale(
  const float target_lux, const float measured_lux) noexcept -> float
{
  return target_lux > 0.0F && measured_lux > 0.0F ? target_lux / measured_lux
                                                  : 1.0F;
}

} // namespace oxygen::vortex::resources
