//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>

namespace oxygen::renderer::internal::shadow_detail {

[[nodiscard]] inline auto ResolvedVirtualPageOverlapsBoundingSphere(
  const renderer::VirtualShadowResolvedRasterPage& page,
  const glm::vec4& world_bounding_sphere) noexcept -> bool
{
  // Missing or degenerate bounds must not cull valid shadow casters.
  if (world_bounding_sphere.w <= 0.0F) {
    return true;
  }

  const glm::vec4 center_ws(
    world_bounding_sphere.x, world_bounding_sphere.y, world_bounding_sphere.z,
    1.0F);
  const glm::vec4 center_ls = page.view_constants.view_matrix * center_ws;
  const glm::vec4 center_clip
    = page.view_constants.projection_matrix * center_ls;
  const float radius = world_bounding_sphere.w;

  // Page-local virtual shadow views are orthographic. W stays at 1, so the
  // projection scales convert a world-space sphere radius into clip-space
  // extents conservatively.
  const glm::mat4& projection = page.view_constants.projection_matrix;
  const float clip_radius_x = std::abs(projection[0][0]) * radius;
  const float clip_radius_y = std::abs(projection[1][1]) * radius;
  const float clip_radius_z = std::abs(projection[2][2]) * radius;
  constexpr float kClipPadding = 1.0e-3F;

  return center_clip.x + clip_radius_x >= (-1.0F - kClipPadding)
    && center_clip.x - clip_radius_x <= (1.0F + kClipPadding)
    && center_clip.y + clip_radius_y >= (-1.0F - kClipPadding)
    && center_clip.y - clip_radius_y <= (1.0F + kClipPadding)
    && center_clip.z + clip_radius_z >= (0.0F - kClipPadding)
    && center_clip.z - clip_radius_z <= (1.0F + kClipPadding);
}

} // namespace oxygen::renderer::internal::shadow_detail
