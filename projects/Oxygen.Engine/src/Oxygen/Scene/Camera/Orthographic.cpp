//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <glm/gtc/matrix_transform.hpp>

namespace oxygen::scene {

/*!
 Computes the orthographic projection matrix for this camera.

 @return The orthographic projection matrix.

 @note The projection matrix is right-handed and z in [0, 1].
  - D3D12: Y+ up, matches Direct3D 12 conventions.
  - Vulkan: Y+ down, matches Vulkan conventions (Y axis flipped).
 @warning Undefined behavior if extents or near/far planes are invalid.
 @see SetExtents, GetExtents
 @see https://en.wikipedia.org/wiki/Orthographic_projection
*/
auto OrthographicCamera::ProjectionMatrix() const -> glm::mat4
{
  glm::mat4 proj = glm::orthoRH_ZO(left_, right_, bottom_, top_, near_, far_);
  if (convention_ == camera::ProjectionConvention::kVulkan) {
    // Flip Y axis for Vulkan (multiply [1][1] by -1)
    proj[1][1] *= -1.0f;
  }
  return proj;
}

/*!
 Maps a screen-space point (in pixels) to a world-space position at the near
 plane using the camera's projection and transform.

 @param p Screen-space point (pixels).
 @param viewport The viewport rectangle.
 @return World-space position at the near plane.
 @see WorldToScreen
*/
auto OrthographicCamera::ScreenToWorld(
  const glm::vec2& p, const glm::vec4& viewport) const -> glm::vec2
{
  DCHECK_NOTNULL_F(transform_);
  const float x = (2.0f * (p.x - viewport.x) / viewport.z) - 1.0f;
  const float y = 1.0f - (2.0f * (p.y - viewport.y) / viewport.w);
  const glm::vec4 ndc(x, y, 1.0f, 1.0f);
  const glm::mat4 view = glm::inverse(transform_->GetWorldMatrix());
  const glm::mat4 proj = ProjectionMatrix();
  const glm::mat4 inv_vp = glm::inverse(proj * view);
  const glm::vec4 world = inv_vp * ndc;
  return { world };
}

/*!
 Projects a world-space position to screen-space (pixels) using the camera's
 projection and transform.

 @param p World-space position.
 @param viewport The viewport rectangle.
 @return Screen-space point (pixels).
 @see ScreenToWorld
*/
auto OrthographicCamera::WorldToScreen(
  const glm::vec2& p, const glm::vec4& viewport) const -> glm::vec2
{
  DCHECK_NOTNULL_F(transform_);
  const glm::vec4 world(p, 0.0f, 1.0f);
  const glm::mat4 view = glm::inverse(transform_->GetWorldMatrix());
  const glm::mat4 proj = ProjectionMatrix();
  const glm::vec4 clip = proj * view * world;
  const float x = ((clip.x + 1.0f) * 0.5f) * viewport.z + viewport.x;
  const float y = ((1.0f - clip.y) * 0.5f) * viewport.w + viewport.y;
  return { x, y };
}

/*!
 Returns the set viewport, or a default rectangle if unset. Used by the renderer
 to determine where to draw the camera's output.

 @return The active viewport rectangle.
 @see SetViewport, GetViewport
*/
auto OrthographicCamera::ActiveViewport() const -> glm::ivec4
{
  return viewport_.value_or(glm::ivec4(0, 0, 0, 0));
}

/*!
 Returns the extents of the camera's box at the near plane, in view space. This
 rectangle is useful for culling and visualization.

 @return (left, bottom, right, top) at the near plane.
 @see SetExtents, GetExtents
*/
auto OrthographicCamera::ClippingRectangle() const -> glm::vec4
{
  return { left_, bottom_, right_, top_ };
}

} // namespace oxygen::scene
