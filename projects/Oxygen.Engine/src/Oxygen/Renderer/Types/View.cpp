//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Renderer/Types/View.h>

namespace oxygen::engine {

View::View(const Params& p)
  : view_(p.view)
  , proj_(p.proj)
  , viewport_(p.viewport)
  , scissor_(p.scissor)
  , pixel_jitter_(p.pixel_jitter)
  , reverse_z_(p.reverse_z)
  , mirrored_(p.mirrored)
{
  inv_view_ = glm::affineInverse(view_);
  inv_proj_ = glm::inverse(proj_);
  view_proj_ = proj_ * view_;
  inv_view_proj_ = glm::inverse(view_proj_);

  if (p.has_camera_position) {
    camera_position_ = p.camera_position;
  } else {
    // Extract camera world position from inverse view (i.e., view-to-world).
    camera_position_ = glm::vec3(inv_view_[3]);
  }

  frustum_ = Frustum::FromViewProj(view_proj_, reverse_z_);

  // Derive vertical focal length in pixels from projection and viewport.
  // For a standard GL/D3D perspective matrix, proj_[1][1] = f = 1/tan(fovY/2).
  // Pixel focal length (vertical) is: f_pixels = f * (viewport_height / 2).
  // For orthographic matrices, proj_[1][1] encodes pixels-per-world-unit in Y
  // scaled to NDC; approximate f_pixels using viewport height and
  // |proj_[1][1]|.
  const float vp_h = static_cast<float>((std::max)(viewport_.w, 0));
  const float m11 = proj_[1][1];
  if (vp_h > 0.0f && std::isfinite(m11) && m11 > 0.0f) {
    focal_length_pixels_ = m11 * (vp_h * 0.5f);
  } else {
    focal_length_pixels_ = 0.0f;
  }
}

} // namespace oxygen::engine
