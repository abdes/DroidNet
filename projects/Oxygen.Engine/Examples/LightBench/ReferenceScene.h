//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

namespace oxygen::examples::light_bench::reference {

inline constexpr float kIlluminanceLux = 1000.0F;
inline constexpr std::array<float, 3> kReflectances { 0.18F, 0.9F, 0.02F };
inline constexpr float kRoughness = 1.0F;
inline constexpr float kExposureKey = 12.5F;
inline constexpr float kDisplayGamma = 2.2F;
inline constexpr float kMinimumViewWidth = 8.0F;
inline constexpr float kMinimumViewHeight = 3.75F;

// Independent roughness=1, NoV=NoL=1 model-2 reference, including F0=0.04.
// E=1-ln(2), B=3.3614294725518486e-5; w=1+F0*(1/E-1),
// T=1-w*(F0*E+(1-F0)*B), L=1000*(F0*w/(4*pi)+albedo*T/pi).
// These are ideal authored values; native tests account separately for packing.
inline constexpr std::array<double, 3> kExpectedLuminance { 59.99767546326685,
  286.1055379496847, 9.751483799618446 };
// Independently resolved log2(expected gray luminance / 0.18), not GPU
// feedback.
inline constexpr float kExposureEv = 8.380765889564564F;

} // namespace oxygen::examples::light_bench::reference
