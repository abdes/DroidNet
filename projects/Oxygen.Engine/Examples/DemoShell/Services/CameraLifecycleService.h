//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::examples::ui {
class CameraRigController;
} // namespace oxygen::examples::ui

namespace oxygen::examples {

//! Lifecycle helper for the active camera in demo modules.
/*!
 Owns active camera selection, viewport application, and reset handling,
 while delegating input to the CameraRigController.
*/
class CameraLifecycleService {
public:
  CameraLifecycleService() = default;
  ~CameraLifecycleService() = default;

  CameraLifecycleService(const CameraLifecycleService&) = delete;
  auto operator=(const CameraLifecycleService&)
    -> CameraLifecycleService& = delete;
  CameraLifecycleService(CameraLifecycleService&&) = default;
  auto operator=(CameraLifecycleService&&) -> CameraLifecycleService& = default;

  //! Set the current scene used for fallback camera creation.
  void SetScene(std::shared_ptr<scene::Scene> scene);

  //! Bind the camera rig controller (optional).
  void BindCameraRig(observer_ptr<ui::CameraRigController> rig);

  //! Assign the active camera node.
  void SetActiveCamera(scene::SceneNode camera);

  //! Access the active camera handle.
  [[nodiscard]] auto GetActiveCamera() -> scene::SceneNode&
  {
    return active_camera_;
  }

  //! Access the active camera handle (const).
  [[nodiscard]] auto GetActiveCamera() const -> const scene::SceneNode&
  {
    return active_camera_;
  }

  //! Capture initial camera pose for reset operations.
  void CaptureInitialPose();

  //! Ensure a valid camera and apply viewport.
  void EnsureViewport(int width, int height);

  //! Align fly camera to face the scene origin if needed.
  void EnsureFlyCameraFacingScene();

  //! Request a rig sync on the next mutation tick.
  void RequestSyncFromActive();

  //! Apply pending rig sync.
  void ApplyPendingSync();

  //! Request a camera reset to initial pose.
  void RequestReset();

  //! Apply pending camera reset.
  void ApplyPendingReset();

  //! Clear camera state when the scene is released.
  void Clear();

private:
  void EnsureFallbackCamera();
  void ApplyViewportToActive(float aspect, const ViewPort& viewport);

  std::shared_ptr<scene::Scene> scene_ {};
  scene::SceneNode active_camera_ {};
  observer_ptr<ui::CameraRigController> camera_rig_ { nullptr };

  glm::vec3 initial_camera_position_ { 0.0F, -15.0F, 0.0F };
  glm::vec3 initial_camera_target_ { 0.0F, 0.0F, 0.0F };
  glm::quat initial_camera_rotation_ { 1.0F, 0.0F, 0.0F, 0.0F };

  bool pending_sync_ { false };
  bool pending_reset_ { false };
};

} // namespace oxygen::examples
