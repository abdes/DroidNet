//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::vortex::environment {

struct HeightFogModel {
  bool enabled { false };
  std::uint32_t legacy_model { 0U };
  bool enable_height_fog { true };
  bool enable_volumetric_fog { false };
  float fog_density { 0.01F };
  float fog_height_falloff { 0.2F };
  float fog_height_offset { 0.0F };
  float second_fog_density { 0.0F };
  float second_fog_height_falloff { 0.0F };
  float second_fog_height_offset { 0.0F };
  glm::vec3 fog_inscattering_luminance { 1.0F, 1.0F, 1.0F };
  glm::vec3 sky_atmosphere_ambient_contribution_color_scale {
    1.0F,
    1.0F,
    1.0F,
  };
  content::ResourceKey inscattering_color_cubemap_resource {};
  float inscattering_color_cubemap_angle { 0.0F };
  glm::vec3 inscattering_texture_tint { 1.0F, 1.0F, 1.0F };
  float fully_directional_inscattering_color_distance { 0.0F };
  float non_directional_inscattering_color_distance { 0.0F };
  glm::vec3 directional_inscattering_luminance { 1.0F, 1.0F, 1.0F };
  float directional_inscattering_exponent { 0.0F };
  float directional_inscattering_start_distance { 0.0F };
  float fog_max_opacity { 1.0F };
  float start_distance { 0.0F };
  float end_distance { 0.0F };
  float fog_cutoff_distance { 0.0F };
  bool holdout { false };
  bool render_in_main_pass { true };
  bool visible_in_reflection_captures { true };
  bool visible_in_real_time_sky_captures { true };
};

} // namespace oxygen::vortex::environment
