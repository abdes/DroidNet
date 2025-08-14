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
}

} // namespace oxygen::engine
