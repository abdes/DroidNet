//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <functional>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>

namespace oxygen::scene {

/*!
 Computes the perspective projection matrix for this camera.

 @return The perspective projection matrix.

 @note The projection matrix is right-handed and z in [0, 1].
  - D3D12: Y+ up, matches Direct3D 12 conventions.
  - Vulkan: Y+ down, matches Vulkan conventions (Y axis flipped).
 @warning Undefined behavior if aspect ratio or near/far planes are invalid.
 @see GetFieldOfView, GetAspectRatio, GetNearPlane, GetFarPlane
 @see https://en.wikipedia.org/wiki/3D_projection
*/
auto PerspectiveCamera::ProjectionMatrix() const -> Mat4
{
  return ProjectionMatrix(ActiveViewport());
}

auto PerspectiveCamera::ProjectionMatrix(const ViewPort& target) const -> Mat4
{
  // Engine canonical projection: right-handed, z in [0,1], reversed-Z.
  const auto proj = MakeReversedZPerspectiveProjectionRH_ZO(
    fov_y_, ResolveAspectRatio(target), near_, far_);
  return proj;
}

auto PerspectiveCamera::ResolveAspectRatio(const ViewPort& target) const
  -> float
{
  return aspect_mode_ == CameraAspectMode::kAuto && target.IsValid()
    ? target.width / target.height
    : aspect_;
}

inline auto PerspectiveCamera::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept -> void
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
auto PerspectiveCamera::ScreenToWorld(const Vec2& p, const Vec4& viewport) const
  -> Vec2
{
  DCHECK_NOTNULL_F(transform_);
  // Convert screen coordinates to normalized device coordinates (NDC)
  const float x = (2.0F * (p.x - viewport.x) / viewport.z) - 1.0F;
  const float y = 1.0F - (2.0F * (p.y - viewport.y) / viewport.w);
  const glm::vec4 ndc(x, y, 1.0F, 1.0F);
  // Compute inverse view-projection matrix
  const glm::mat4 view = glm::inverse(transform_->GetWorldMatrix());
  const Mat4 proj = ProjectionMatrix(ViewPort {
    .top_left_x = viewport.x,
    .top_left_y = viewport.y,
    .width = viewport.z,
    .height = viewport.w,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  const glm::mat4 inv_vp = glm::inverse(proj * view);
  glm::vec4 world = inv_vp * ndc;
  if (world.w != 0.0F) {
    world /= world.w;
  }
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
auto PerspectiveCamera::WorldToScreen(const Vec2& p, const Vec4& viewport) const
  -> Vec2
{
  DCHECK_NOTNULL_F(transform_);
  const glm::vec4 world(p.x, p.y, 0.0F, 1.0F);
  const glm::mat4 view = glm::inverse(transform_->GetWorldMatrix());
  const Mat4 proj = ProjectionMatrix(ViewPort {
    .top_left_x = viewport.x,
    .top_left_y = viewport.y,
    .width = viewport.z,
    .height = viewport.w,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  glm::vec4 clip = proj * view * world;
  if (clip.w != 0.0F) {
    clip /= clip.w;
  }
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
auto PerspectiveCamera::ActiveViewport() const -> ViewPort
{
  return viewport_.value_or(ViewPort {});
}

/*!
 Returns the extents of the camera's frustum at the near plane, in view space.
 This rectangle is useful for culling and visualization.

 @return (left, bottom, right, top) at the near plane.

 @see https://en.wikipedia.org/wiki/3D_projection
 @see GetFieldOfView, GetAspectRatio, GetNearPlane
*/
auto PerspectiveCamera::ClippingRectangle() const -> Vec4
{
  // Return the horizontal/vertical FOV extents at the near plane in view space
  const float tan_half_fov = std::tan(fov_y_ * 0.5F);
  const float nh = near_ * tan_half_fov;
  const float nw = nh * ResolveAspectRatio(ActiveViewport());
  // (left, bottom, right, top) at near plane
  return { -nw, -nh, nw, nh };
}

} // namespace oxygen::scene
