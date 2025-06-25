//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <optional>

#include <glm/glm.hpp>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/Camera/ProjectionConvention.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

//! Orthographic camera component for 3D scene nodes
/*!
 Implements an orthographic projection camera for use in 3D scenes. This camera
 projects 3D points onto a 2D image plane using a cuboid (box) frustum, with no
 perspective foreshortening. Useful for 2D games, UI, CAD, and isometric views.

 ### Key Features
 - **Orthographic projection**: No perspective; objects retain their size
   regardless of depth.
 - **Configurable extents**: Set left, right, bottom, top, near, and far planes.
 - **Viewport support**: Allows rendering to subregions of the screen.
 - **Scene node integration**: Always attached to a node with a transform.

 ### Usage Patterns
 Attach to a scene node to define an orthographic view for rendering. Adjust
 extents to match the desired view region and output resolution. Use the
 transform component for camera position and orientation.

 ### Architecture Notes
 - The camera's transform is always provided by the owning node's
   TransformComponent.
 - Projection parameters are independent of position/orientation.
 - The near and far planes limit the visible range and affect depth buffer
   precision.

 @note Orthographic projection does not perform perspective division; w is
 always 1.0 after projection.
 @see TransformComponent
*/
class OrthographicCamera : public Component {
  OXYGEN_COMPONENT(OrthographicCamera)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  //! Creates a OrthographicCamera using the given projection \p convention.
  explicit OrthographicCamera(const camera::ProjectionConvention convention)
    : convention_(convention)
  {
  }

  //! Virtual destructor
  ~OrthographicCamera() override = default;

  OXYGEN_DEFAULT_COPYABLE(OrthographicCamera)
  OXYGEN_DEFAULT_MOVABLE(OrthographicCamera)

  //! Sets the orthographic extents (left, right, bottom, top, near, far).
  /*!
   Sets the orthographic projection extents. These define the cuboid frustum.
   @param left       Left plane in view space.
   @param right      Right plane in view space.
   @param bottom     Bottom plane in view space.
   @param top        Top plane in view space.
   @param near_plane Near plane distance.
   @param far_plane  Far plane distance.
   @see GetExtents
  */
  auto SetExtents(const float left, const float right, const float bottom,
    const float top, const float near_plane, const float far_plane) -> void
  {
    left_ = left;
    right_ = right;
    bottom_ = bottom;
    top_ = top;
    near_ = near_plane;
    far_ = far_plane;
  }

  //! Gets the orthographic extents as (left, right, bottom, top, near, far).
  /*!
   @return The extents as a std::array of 6 floats.
   @see SetExtents
  */
  OXGN_SCN_NDAPI auto GetExtents() const -> std::array<float, 6>
  {
    return { left_, right_, bottom_, top_, near_, far_ };
  }

  //! Sets the viewport rectangle for this camera.
  auto SetViewport(const glm::ivec4& viewport) -> void { viewport_ = viewport; }

  //! Resets the viewport to unset (full target).
  auto ResetViewport() -> void { viewport_.reset(); }

  //! Returns the current viewport rectangle if set, or std::nullopt if unset.
  OXGN_SCN_NDAPI auto GetViewport() const -> std::optional<glm::ivec4>
  {
    return viewport_;
  }

  //! Maps a screen-space point (in pixels) to a world-space position at the
  //! near plane.
  OXGN_SCN_NDAPI auto ScreenToWorld(
    const glm::vec2& p, const glm::vec4& viewport) const -> glm::vec2;

  //! Projects a world-space position to screen-space (pixels).
  OXGN_SCN_NDAPI auto WorldToScreen(
    const glm::vec2& p, const glm::vec4& viewport) const -> glm::vec2;

  //! Returns the set viewport, or a default rectangle if unset.
  OXGN_SCN_NDAPI auto ActiveViewport() const -> glm::ivec4;

  //! Returns the extents of the camera's box at the near plane, in view space.
  OXGN_SCN_NDAPI auto ClippingRectangle() const -> glm::vec4;

  //! Computes the orthographic projection matrix for this camera.
  OXGN_SCN_NDAPI auto ProjectionMatrix() const -> glm::mat4;

  //! Gets the current projection convention.
  /*! @return The current projection convention. */
  [[nodiscard]] auto GetProjectionConvention() const
    -> camera::ProjectionConvention
  {
    return convention_;
  }

protected:
  auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override
  {
    transform_ = &static_cast<detail::TransformComponent&>(
      get_component(detail::TransformComponent::ClassTypeId()));
  }

private:
  float left_ = -1.0f;
  float right_ = 1.0f;
  float bottom_ = -1.0f;
  float top_ = 1.0f;
  float near_ = 0.1f;
  float far_ = 1000.0f;
  std::optional<glm::ivec4> viewport_;
  detail::TransformComponent* transform_ { nullptr };
  camera::ProjectionConvention convention_;
};

} // namespace oxygen::scene
