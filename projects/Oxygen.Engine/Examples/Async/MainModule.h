//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Scene/Scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "../Common/AsyncEngineApp.h"
#include "../Common/ExampleModuleBase.h"

namespace oxygen::examples::async {

//! Graphics module demonstrating AsyncEngine and Common example patterns.
/*!
 This module demonstrates integrated rendering using ExampleModuleBase,
 AppWindow, and RenderGraph from Examples/Common. It showcases:

 - Using ExampleModuleBase for common lifecycle (window, surface, passes)
 - Async-specific features: drone camera, animation, scene graph
 - Phase-based rendering coordination via AsyncEngine
 - Input binding for drone speed control

 @see ExampleModuleBase, AsyncEngine, Graphics
*/
class MainModule final : public common::ExampleModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = ExampleModuleBase;

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  //! Constructor using the aggregated AsyncEngineApp state.
  /*!
   @param app Aggregated application context (non-owning reference)
  */
  explicit MainModule(const common::AsyncEngineApp& app);

  ~MainModule() override;

  //! Module identification and priority.
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "GraphicsMainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::ModulePriority { 500 }; // Normal priority
  }

  //! Register for graphics-related phases.
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override;

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true; // Critical module
  }

  //! Customize window properties for this example.
  auto BuildDefaultWindowProperties() const
    -> oxygen::platform::window::Properties override;

  //! Example-specific setup: scene, input, and animation.
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;

  //! Execute phase-specific work.
  auto OnFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnTransformPropagation(engine::FrameContext& context)
    -> co::Co<> override;
  auto OnFrameGraph(engine::FrameContext& context) -> co::Co<> override;
  auto OnCommandRecord(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;

private:
  //! Setup functions (called once).
  auto SetupShaders() -> void;
  // Input actions/mappings for camera drone speed control
  auto SetupInput() -> void;

  //! Scene and rendering functions.
  auto EnsureExampleScene() -> void;
  auto EnsureMainCamera(int width, int height) -> void;
  // delta_time used to be absolute time; module now expects a per-frame
  // delta (seconds) for update integration. Use double precision here to
  // preserve granularity at high FPS.
  auto UpdateAnimations(double delta_time) -> void;
  auto UpdateSceneMutations(float delta_time) -> void;

  //! Debug overlay methods.
  auto DrawDebugOverlay(engine::FrameContext& context) -> void;
  auto DrawPerformancePanel() -> void;
  auto DrawFrameActionsPanel() -> void;
  auto DrawSceneInfoPanel() -> void;
  auto DrawRenderPassesPanel() -> void;
  auto TrackPhaseStart(const std::string& phase_name) -> void;
  auto TrackPhaseEnd() -> void;
  auto TrackFrameAction(const std::string& action) -> void;
  auto StartFrameTracking() -> void;
  auto EndFrameTracking() -> void;

  //! Command recording (execute render graph).
  auto ExecuteRenderCommands(engine::FrameContext& context) -> co::Co<>;

  //! Dependencies (aggregated app context, non-owning).
  const oxygen::examples::common::AsyncEngineApp& app_;

  //! Scene and rendering.
  std::shared_ptr<scene::Scene> scene_;

  //! State tracking.
  bool initialized_ { false };
  std::chrono::steady_clock::time_point start_time_;
  // Last engine frame timestamp observed by this module. Used to compute
  // per-frame delta time for smooth integration of animations.
  std::chrono::steady_clock::time_point last_frame_time_ {};
  // Elapsed animation time in seconds since module start, computed from
  // engine frame timestamp for stable sampling across phases.
  double anim_time_ { 0.0 };

  //! Debug overlay tracking structures
  struct FrameActionTracker {
    std::chrono::steady_clock::time_point frame_start_time;
    std::chrono::steady_clock::time_point frame_end_time;
    std::vector<std::pair<std::string, std::chrono::microseconds>>
      phase_timings;
    std::vector<std::string> frame_actions;
    std::uint32_t spheres_updated { 0 };
    std::uint32_t render_items_count { 0 };
    bool scene_mutation_occurred { false };
    bool transform_propagation_occurred { false };
    bool frame_graph_setup { false };
    bool command_recording { false };
  };

  FrameActionTracker current_frame_tracker_;
  std::vector<FrameActionTracker> frame_history_;
  static constexpr std::size_t kMaxFrameHistory = 60; // Keep 1 second at 60fps

  // Per-phase timing helpers
  std::chrono::steady_clock::time_point phase_start_time_;
  std::string current_phase_name_;

  // Per-sphere animation state (multiple spheres with different speeds)
  struct SphereState {
    scene::SceneNode node;
    // Base phases used for absolute-time evaluation (no per-frame drift)
    double base_angle { 0.0 };
    double speed { 0.6 }; // radians/sec
    double radius { 4.0 }; // orbit radius in world units
    double inclination { 0.5 }; // tilt of orbital plane (radians)
    double spin_speed { 0.0 }; // self-rotation speed (radians/sec)
    double base_spin_angle { 0.0 }; // initial spin phase
  };

  std::vector<SphereState> spheres_;

  //! Scene nodes for the example.
  scene::SceneNode multisubmesh_; // Per-submesh visibility/overrides
  scene::SceneNode main_camera_; // "MainCamera"

  // Camera view for the main viewport
  std::shared_ptr<renderer::CameraView> camera_view_;
  // ViewId for the main viewport
  ViewId view_id_ { 0 };

  //! Animation state (quad rotation removed; sphere orbits, camera fixed).
  int last_vis_toggle_ { -1 };
  int last_ovr_toggle_ { -1 };
  // Encapsulated camera drone state and behavior
  struct CameraDrone {
    bool enabled { true };
    double angle { 0.0 };
    double radius { 15.0 };
    double speed { 0.2 }; // radians/sec
    double inclination { 0.15 }; // tilt in radians

    // Drone-style dynamics
    glm::vec3 current_pos { 0.0f, 0.0f, 0.0f };
    glm::quat current_rot { 1.0f, 0.0f, 0.0f, 0.0f };
    bool initialized { false };
    // Maximum allowed linear speed (world units per second) to cap sudden
    // motion
    double max_speed { 7.0 };
    // Ramp parameters to smoothly introduce drone motion
    double ramp_time { 2.0 }; // seconds during which motion ramps up
    double ramp_elapsed { 0.0 }; // internal accumulator
    // Focus point offsets: camera will look at (focus_offset.x, focus_height,
    // focus_offset.y)
    glm::vec2 focus_offset { 0.0f, 0.0f };
    float focus_height { 0.8f }; // target height to look at (world units)
    // Flight path spline (closed Catmull-Rom control points in world space)
    std::vector<glm::vec3> path_points;
    double path_length { 0.0 }; // cached approximate length
    double path_speed {
      6.0
    }; // world units per second along path (increased for quicker survey)
    double path_u { 0.0 }; // current parameter along path [0,1)
    // Arc-length traversal state: advance by distance, not param u
    double path_s { 0.0 }; // current arc-length position in [0, path_length)
    struct ArcLengthLut {
      std::vector<double> u_samples; // monotonically increasing in [0,1]
      std::vector<double> s_samples; // cumulative lengths in [0, path_length]
    } arc_lut;
    // (streamlined) gimbal dynamics were removed to keep the demo focused
    // on a single body-controlled camera with gentle smoothing and banking.
    // Points of interest the camera may slow near for inspection
    std::vector<glm::vec3> pois;
    double damping { 8.0 }; // higher = stiffer follow
    double bob_amp {
      0.06
    }; // vertical bob amplitude (reduced to avoid vibration)
    double bob_freq { 1.6 }; // bob frequency (Hz)
    double noise_amp { 0.03 }; // lateral jitter amplitude (reduced)
    double bank_factor { 0.045 }; // how much bank per linear speed
    double max_bank { 0.45 }; // max roll in radians
    // Procedural noise smoothing state to reduce high-frequency vibration
    glm::vec2 noise_state { 0.0f, 0.0f };
    float noise_response { 8.0f }; // responsiveness for noise smoothing (Hz)
    float lateral_osc_amp { 0.03f }; // lateral oscillation magnitude
  } camera_drone_;

  // Camera update helper
  void InitializeDefaultFlightPath();
  auto UpdateCameraDrone(double delta_time) -> void;

  // --- Input actions for drone speed control ---
  std::shared_ptr<oxygen::input::Action> action_speed_up_;
  std::shared_ptr<oxygen::input::Action> action_speed_down_;
  std::shared_ptr<oxygen::input::InputMappingContext> input_ctx_;
  // Token for a registered platform pre-destroy callback; zero means none.
  size_t platform_window_destroy_handler_token_ { 0 };
};

} // namespace oxygen::examples::async
