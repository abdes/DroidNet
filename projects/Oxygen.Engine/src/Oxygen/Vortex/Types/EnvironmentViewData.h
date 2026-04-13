//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/vec4.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Atmosphere.h>

namespace oxygen::vortex {

//! Environment-owned per-view atmosphere context and controls.
struct alignas(packing::kShaderDataFieldAlignment) EnvironmentViewData {
  static constexpr size_t kSize = 64;

  uint32_t flags { 0U };
  float sky_view_lut_slice { 0.0F };
  float planet_to_sun_cos_zenith { 0.0F };
  float aerial_perspective_distance_scale {
    atmos::kDefaultAerialPerspectiveDistanceScale
  };
  float aerial_scattering_strength { atmos::kDefaultAerialScatteringStrength };
  std::array<uint32_t, 3> _pad { 0, 0, 0 };
  glm::vec4 planet_center_ws_pad { 0.0F, 0.0F, -atmos::kDefaultPlanetRadiusM,
    0.0F };
  glm::vec4 planet_up_ws_camera_altitude_m { atmos::kDefaultPlanetUp, 0.0F };
};

static_assert(sizeof(EnvironmentViewData) == EnvironmentViewData::kSize);
static_assert(
  alignof(EnvironmentViewData) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(EnvironmentViewData) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::vortex
