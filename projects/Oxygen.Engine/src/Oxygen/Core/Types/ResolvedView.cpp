//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ResolvedView.h>

namespace oxygen {

ResolvedView::ResolvedView(const Params& p)
  : config_(p.view_config)
  , view_(p.view_matrix)
  , proj_(p.proj_matrix)
  , stable_proj_(p.stable_proj_matrix.value_or(p.proj_matrix))
  , viewport_(p.view_config.viewport)
  , scissor_(p.view_config.scissor)
  , pixel_jitter_(p.view_config.pixel_jitter)
  , reverse_z_(p.view_config.reverse_z)
  , mirrored_(p.view_config.mirrored)
  , near_plane_(p.near_plane)
  , far_plane_(p.far_plane)
  , depth_range_(p.depth_range)
{
  CHECK_F(
    std::isfinite(near_plane_) && (IsOrthographic() || near_plane_ > 0.0F),
    "ResolvedView: near_plane must be finite and positive for perspective "
    "projection (got {})",
    near_plane_);
  CHECK_F(std::isfinite(far_plane_) && far_plane_ > near_plane_,
    "ResolvedView: far_plane must be finite and > near_plane (got {})",
    far_plane_);

  inv_view_ = glm::affineInverse(view_);
  inv_proj_ = glm::inverse(proj_);
  view_proj_ = proj_ * view_;
  inv_view_proj_ = glm::inverse(view_proj_);

  if (p.camera_position) {
    camera_position_ = *p.camera_position;
  } else {
    // Extract camera world position from inverse view (i.e., view-to-world).
    constexpr auto kTranslationColumn = 3;
    camera_position_ = glm::vec3(glm::column(inv_view_, kTranslationColumn));
  }

  camera_ev_ = p.camera_ev;

  frustum_ = Frustum::FromViewProj(view_proj_, reverse_z_);

  // Derive vertical focal length in pixels from projection and viewport.
  const float vp_h = std::max(viewport_.height, 0.0F);
  constexpr auto kVerticalScaleColumn = 1;
  const float m11 = glm::column(proj_, kVerticalScaleColumn).y;
  if (vp_h > 0.0F && std::isfinite(m11) && m11 > 0.0F) {
    focal_length_pixels_ = m11 * (vp_h * 0.5F);
  } else {
    focal_length_pixels_ = 0.0F;
  }
}

auto ResolvedView::IsOrthographic() const noexcept -> bool
{
  constexpr auto kHomogeneousRow = 3;
  const auto clip_w = glm::row(proj_, kHomogeneousRow);
  return clip_w.x == 0.0F && clip_w.y == 0.0F && clip_w.z == 0.0F
    && std::isfinite(clip_w.w) && clip_w.w > 0.0F;
}

} // namespace oxygen
