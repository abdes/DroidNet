//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <glm/glm.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::textured_cube {

//! Manages an orbit camera with mouse-based controls.
/*!
 This class encapsulates all camera orbit logic including:
 - Setting up input bindings (zoom, orbit via RMB+drag)
 - Creating and configuring a perspective camera
 - Computing camera position from orbit parameters
 - Handling zoom and orbit input each frame

 ### Usage

 ```cpp
 CameraController controller(input_system);
 controller.EnsureCamera(scene, width, height);
 controller.Update(); // Call each frame
 ```
*/
class CameraController final {
public:
  //! Configuration for the orbit camera.
  struct Config {
    glm::vec3 target { 0.0f, 0.0f, 0.0f };
    float initial_yaw_rad { -glm::half_pi<float>() };
    float initial_pitch_rad { 0.4f }; //!< ~23 degrees above horizon
    float initial_distance { 6.0f };
    float orbit_sensitivity { 0.01f };
    float zoom_step { 0.75f };
    float min_distance { 1.25f };
    float max_distance { 40.0f };
    float fov_degrees { 60.0f };
    float near_plane { 0.05f };
    float far_plane { 500.0f };
  };

  explicit CameraController(
    oxygen::observer_ptr<oxygen::engine::InputSystem> input_system,
    const Config& config = {});

  ~CameraController() = default;

  CameraController(const CameraController&) = delete;
  auto operator=(const CameraController&) -> CameraController& = delete;
  CameraController(CameraController&&) = delete;
  auto operator=(CameraController&&) -> CameraController& = delete;

  //! Initialize input bindings for camera control.
  auto InitInputBindings() noexcept -> bool;

  //! Ensure a camera node exists in the scene with proper configuration.
  auto EnsureCamera(std::shared_ptr<scene::Scene>& scene, int width, int height)
    -> void;

  //! Update camera based on input actions (call each frame).
  auto Update() -> void;

  //! Get the camera scene node.
  [[nodiscard]] auto GetCameraNode() const -> scene::SceneNode
  {
    return camera_node_;
  }

  //! Check if camera is ready for rendering.
  [[nodiscard]] auto IsReady() const -> bool { return camera_node_.IsAlive(); }

  //! Get orbit parameters for debug display.
  [[nodiscard]] auto GetOrbitYaw() const -> float { return orbit_yaw_rad_; }
  [[nodiscard]] auto GetOrbitPitch() const -> float { return orbit_pitch_rad_; }
  [[nodiscard]] auto GetDistance() const -> float { return orbit_distance_; }
  [[nodiscard]] auto GetTarget() const -> glm::vec3 { return config_.target; }

private:
  auto ApplyOrbitAndZoom() -> void;

  oxygen::observer_ptr<oxygen::engine::InputSystem> input_system_;
  Config config_;

  scene::SceneNode camera_node_;

  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action> rmb_action_;
  std::shared_ptr<oxygen::input::Action> orbit_action_;
  std::shared_ptr<oxygen::input::InputMappingContext> camera_controls_ctx_;

  float orbit_yaw_rad_;
  float orbit_pitch_rad_;
  float orbit_distance_;
};

} // namespace oxygen::examples::textured_cube
