//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ResolvedView.h>

namespace oxygen {

ResolvedView::ResolvedView(const Params& p)
  : config_(p.view_config)
  , view_(p.view_matrix)
  , proj_(p.proj_matrix)
  , viewport_(p.view_config.viewport)
  , scissor_(p.view_config.scissor)
  , pixel_jitter_(p.view_config.pixel_jitter)
  , reverse_z_(p.view_config.reverse_z)
  , mirrored_(p.view_config.mirrored)
  , near_plane_(p.near_plane)
  , far_plane_(p.far_plane)
  , depth_range_(p.depth_range)
{
  CHECK_F(std::isfinite(near_plane_) && near_plane_ > 0.0F,
    "ResolvedView: near_plane must be finite and > 0 (got %f)", near_plane_);
  CHECK_F(std::isfinite(far_plane_) && far_plane_ > near_plane_,
    "ResolvedView: far_plane must be finite and > near_plane (got %f)",
    far_plane_);

  inv_view_ = glm::affineInverse(view_);
  inv_proj_ = glm::inverse(proj_);
  view_proj_ = proj_ * view_;
  inv_view_proj_ = glm::inverse(view_proj_);

  if (p.camera_position) {
    camera_position_ = *p.camera_position;
  } else {
    // Extract camera world position from inverse view (i.e., view-to-world).
    camera_position_ = glm::vec3(inv_view_[3]);
  }

  camera_ev_ = p.camera_ev;

  frustum_ = Frustum::FromViewProj(view_proj_, reverse_z_);

  // Derive vertical focal length in pixels from projection and viewport.
  const float vp_h = static_cast<float>(std::max(viewport_.height, 0.0f));
  const float m11 = proj_[1][1];
  if (vp_h > 0.0f && std::isfinite(m11) && m11 > 0.0f) {
    focal_length_pixels_ = m11 * (vp_h * 0.5f);
  } else {
    focal_length_pixels_ = 0.0f;
  }
}

} // namespace oxygen
