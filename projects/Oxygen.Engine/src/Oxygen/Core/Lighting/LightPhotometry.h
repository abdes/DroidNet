//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/api_export.h>
#include <Oxygen/Base/Result.h>

#include <glm/ext/vector_float3.hpp>

namespace oxygen::lighting {

enum class LightPhotometryError : std::uint8_t {
  kInvalidInput,
  kUnrepresentable,
};

//! Authored modifiers; camera exposure never enters light resolution.
struct LightPhotometryModifiers {
  glm::vec3 color_rgb { 1.0F };
  float exposure_compensation_ev { 0.0F };
};

//! Precomputed FP32 cone parameters and the CPU flux-normalization integral.
struct SpotConeProfile {
  float outer_cosine { 0.0F };
  float inverse_cosine_width { 0.0F };
  double solid_angle_sr { 0.0 };
};

OXGN_CORE_NDAPI auto ResolveSpotConeProfile(
  float inner_half_angle_radians, float outer_half_angle_radians)
  -> Result<SpotConeProfile, LightPhotometryError>;

OXGN_CORE_NDAPI auto ResolveDirectionalIlluminanceRgb(
  float illuminance_lux, const LightPhotometryModifiers& modifiers)
  -> Result<glm::vec3, LightPhotometryError>;

OXGN_CORE_NDAPI auto ResolvePointIntensityRgb(
  float luminous_flux_lm, const LightPhotometryModifiers& modifiers)
  -> Result<glm::vec3, LightPhotometryError>;

OXGN_CORE_NDAPI auto ResolveSpotIntensityRgb(float luminous_flux_lm,
  const SpotConeProfile& cone, const LightPhotometryModifiers& modifiers)
  -> Result<glm::vec3, LightPhotometryError>;

} // namespace oxygen::lighting
