//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

namespace detail {
  class TransformComponent;
} // namespace detail

//! Perspective camera component for 3D scene nodes
/*!
 Implements a perspective projection camera for use in 3D scenes. This camera
 models the way a real-world pinhole camera or human eye projects 3D points onto
 a 2D image plane, using a frustum defined by field of view, aspect ratio, and
 near/far clipping planes.

 ### Key Features
 - **Perspective projection**: Simulates depth and foreshortening as in real
   cameras. Objects farther from the camera appear smaller, mimicking real-world
   depth perception.
 - **Configurable FOV, aspect, near/far**: Matches real camera lens and film
   properties. The projection matrix encodes these parameters, defining the
   visible view frustum.
 - **Perspective division**: After transformation, 3D points are divided by
   their w coordinate, scaling x and y by depth. This creates the illusion of
   depth and perspective in the final 2D image.
 - **Viewport support**: Allows rendering to subregions of the screen.
 - **Scene node integration**: Always attached to a node with a transform.

 ### Usage Patterns
 Attach to a scene node to define the view for rendering. Adjust FOV and aspect
 to match the desired lens and output resolution. Use the transform component
 for camera position and orientation.

 ### Architecture Notes
 - The camera's transform is always provided by the owning node's
   TransformComponent.
 - Projection parameters are independent of position/orientation.
 - The near and far planes limit the visible range and affect depth buffer
   precision.

   @note Perspective division (dividing by w) is what creates the depth effect
 in the final 2D image. Without it, the scene would appear flat and lack
 realistic depth cues.
 @see TransformComponent
*/
class PerspectiveCamera : public Component {
  OXYGEN_COMPONENT(PerspectiveCamera)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  //! Creates a default PerspectiveCamera using the engine canonical projection.
  PerspectiveCamera() = default;

  //! Virtual destructor
  ~PerspectiveCamera() override = default;

  OXYGEN_DEFAULT_COPYABLE(PerspectiveCamera)
  OXYGEN_DEFAULT_MOVABLE(PerspectiveCamera)

  //! Sets the vertical field of view (FOV) in radians.
  /*!
   Sets the vertical field of view (FOV) in radians. The FOV determines how wide
   the camera sees, simulating the lens of a real camera. Typical values are
   between 45° and 90°.
   @param fov_y_radians Vertical FOV in radians.

   @note Changing FOV affects the perspective and sense of depth.
   @warning Setting FOV to 0 or extreme values may cause rendering artifacts.
   @see GetFieldOfView
  */
  auto SetFieldOfView(const float fov_y_radians) -> void
  {
    fov_y_ = fov_y_radians;
  }

  //! Gets the vertical field of view (FOV) in radians.
  /*!
   @return The vertical FOV in radians.
   @see SetFieldOfView
  */
  OXGN_SCN_NDAPI auto GetFieldOfView() const -> float { return fov_y_; }

  //! Sets the aspect ratio (width/height) of the camera's view.
  /*!
   Sets the aspect ratio (width/height) of the camera's view. The aspect ratio
   should match the output image or viewport.
   @param aspect Width divided by height.
   @return None
   @note Aspect ratio affects the shape of the frustum.
   @see GetAspectRatio
  */
  auto SetAspectRatio(const float aspect) -> void { aspect_ = aspect; }

  //! Gets the aspect ratio (width/height).
  /*!
   @return The aspect ratio.
   @see SetAspectRatio
  */
  OXGN_SCN_NDAPI auto GetAspectRatio() const -> float { return aspect_; }

  //! Sets the near clipping plane distance.
  /*!
   Sets the near clipping plane distance. Objects closer than this are not
   rendered.
   @param near_plane Near plane distance (> 0).
   @return None
   @warning Setting near_plane <= 0 may cause undefined behavior.
   @see GetNearPlane
  */
  auto SetNearPlane(const float near_plane) -> void { near_ = near_plane; }

  //! Gets the near clipping plane distance.
  /*!
   @return The near plane distance.
   @see SetNearPlane
  */
  OXGN_SCN_NDAPI auto GetNearPlane() const -> float { return near_; }

  //! Sets the far clipping plane distance.
  /*!
   Sets the far clipping plane distance. Objects farther than this are not
   rendered.
   @param far_plane Far plane distance.
   @return None
   @see GetFarPlane
  */
  auto SetFarPlane(const float far_plane) -> void { far_ = far_plane; }

  //! Gets the far clipping plane distance.
  /*!
   @return The far plane distance.
   @see SetFarPlane
  */
  OXGN_SCN_NDAPI auto GetFarPlane() const -> float { return far_; }

  //! Sets the viewport rectangle for this camera.
  /*!
    Sets the viewport rectangle, defining the region of the render target
    (screen or texture) where the camera's output will be drawn. Useful for
    split-screen or editor views.
    @param viewport ViewPort struct with position, size, and depth range.
    @note Setting the viewport overrides the default full-target rendering.
    @see Viewport, ActiveViewport
  */
  auto SetViewport(const ViewPort& viewport) -> void { viewport_ = viewport; }

  //! Resets the viewport to unset (full target).
  /*!
    Resets the viewport, so the camera will render to the full output region.
    @note After reset, Viewport() returns std::nullopt.
    @see SetViewport, Viewport
  */
  auto ResetViewport() -> void { viewport_.reset(); }

  /*!
    Returns the current viewport rectangle if set, or std::nullopt if unset.
    @return The viewport rectangle, or std::nullopt if unset.
    @see SetViewport, ResetViewport
  */
  OXGN_SCN_NDAPI auto GetViewport() const -> std::optional<ViewPort>
  {
    return viewport_;
  }

  //! Maps a screen-space point (in pixels) to a world-space position at the
  //! near plane.
  OXGN_SCN_NDAPI auto ScreenToWorld(const Vec2& p, const Vec4& viewport) const
    -> Vec2;

  //! Projects a world-space position to screen-space (pixels).
  OXGN_SCN_NDAPI auto WorldToScreen(const Vec2& p, const Vec4& viewport) const
    -> Vec2;

  //! Returns the set viewport, or a default rectangle if unset.
  OXGN_SCN_NDAPI auto ActiveViewport() const -> ViewPort;

  //! Returns the extents of the camera's frustum at the near plane, in view
  //! space.
  OXGN_SCN_NDAPI auto ClippingRectangle() const -> Vec4;

  //! Computes the perspective projection matrix for this camera (engine
  //! canonical).
  OXGN_SCN_NDAPI auto ProjectionMatrix() const -> Mat4;

protected:
  OXGN_SCN_API auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override;

private:
  static constexpr float kDefaultNearPlane = 0.1F;
  static constexpr float kDefaultFarPlane = 1000.0F;

  float fov_y_ = 1.0F;
  float aspect_ = 1.0F;
  float near_ = kDefaultNearPlane;
  float far_ = kDefaultFarPlane;
  std::optional<ViewPort> viewport_;
  detail::TransformComponent* transform_ { nullptr };
};

} // namespace oxygen::scene
