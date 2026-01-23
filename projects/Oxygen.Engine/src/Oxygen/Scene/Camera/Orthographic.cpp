//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Constants.h>
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
auto OrthographicCamera::ProjectionMatrix() const -> Mat4
{
  // Engine canonical orthographic projection: right-handed, z in [0,1], no
  // Y-flip.
  Mat4 proj = glm::orthoRH_ZO(left_, right_, bottom_, top_, near_, far_);
  return proj;
}

void OrthographicCamera::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept
{
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  transform_ = &static_cast<detail::TransformComponent&>(
    get_component(detail::TransformComponent::ClassTypeId()));
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
  const Vec2& p, const Vec4& viewport) const -> Vec2
{
  DCHECK_NOTNULL_F(transform_);
  const float x = (2.0F * (p.x - viewport.x) / viewport.z) - 1.0F;
  const float y = 1.0F - (2.0F * (p.y - viewport.y) / viewport.w);
  const glm::vec4 ndc(x, y, 1.0F, 1.0F);
  const glm::mat4 view = glm::inverse(transform_->GetWorldMatrix());
  const Mat4 proj = ProjectionMatrix();
  const glm::mat4 inv_vp = glm::inverse(proj * view);
  const glm::vec4 world = inv_vp * ndc;
  return { world.x, world.y };
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
  const Vec2& p, const Vec4& viewport) const -> Vec2
{
  DCHECK_NOTNULL_F(transform_);
  const glm::vec4 world(p.x, p.y, 0.0F, 1.0F);
  const glm::mat4 view = glm::inverse(transform_->GetWorldMatrix());
  const Mat4 proj = ProjectionMatrix();
  const glm::vec4 clip = proj * view * world;
  const float x = (((clip.x + 1.0F) * 0.5F) * viewport.z) + viewport.x;
  const float y = (((1.0F - clip.y) * 0.5F) * viewport.w) + viewport.y;
  return { x, y };
}

/*!
 Returns the set viewport, or a default rectangle if unset. Used by the renderer
 to determine where to draw the camera's output.

 @return The active viewport rectangle.
 @see SetViewport, GetViewport
*/
auto OrthographicCamera::ActiveViewport() const -> ViewPort
{
  return viewport_.value_or(ViewPort {});
}

/*!
 Returns the extents of the camera's box at the near plane, in view space. This
 rectangle is useful for culling and visualization.

 @return (left, bottom, right, top) at the near plane.
 @see SetExtents, GetExtents
*/
auto OrthographicCamera::ClippingRectangle() const -> Vec4
{
  return { left_, bottom_, right_, top_ };
}

} // namespace oxygen::scene
