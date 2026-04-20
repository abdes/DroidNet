//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct GroundGridConfig {
  bool enabled { true };
  float spacing { 1.0F };
  std::uint32_t major_every { 10U };
  float line_thickness { 0.02F };
  float major_thickness { 0.04F };
  float axis_thickness { 0.06F };
  float fade_start { 0.0F };
  float fade_power { 2.0F };
  float horizon_boost { 0.35F };
  Vec2 origin { 0.0F, 0.0F };
  bool smooth_motion { true };
  float smooth_time { 1.0F };
  graphics::Color minor_color { 0.16F, 0.16F, 0.16F, 1.0F };
  graphics::Color major_color { 0.20F, 0.20F, 0.20F, 1.0F };
  graphics::Color axis_color_x { 0.7F, 0.23F, 0.23F, 1.0F };
  graphics::Color axis_color_y { 0.23F, 0.7F, 0.23F, 1.0F };
  graphics::Color origin_color { 1.0F, 1.0F, 1.0F, 1.0F };
};

} // namespace oxygen::vortex
