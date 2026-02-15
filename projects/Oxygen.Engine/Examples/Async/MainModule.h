//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/SceneNode.h>

#include "Async/AsyncDemoTypes.h"
#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "Oxygen/Core/PhaseRegistry.h"

namespace oxygen::examples::ui {
class CameraRigController;
}

namespace oxygen::examples::async {

class AsyncDemoPanel;
class AsyncDemoVm;
class AsyncDemoSettingsService;

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
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = DemoModuleBase;

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  //! Constructor using the aggregated AsyncEngineApp state.
  /*!
   @param app Aggregated application context (non-owning reference)
  */
  explicit MainModule(const DemoAppContext& app);

  ~MainModule() override;

  //! Module identification and priority.
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "GraphicsMainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr auto kPriority = engine::ModulePriority { 500 };
    return kPriority; // Normal priority
  }

  //! Register for graphics-related phases.
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay,
      kPublishViews, kGuiUpdate, kPreRender, kCompositing, kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true; // Critical module
  }

  //! Customize window properties for this example.
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  //! Example-specific setup: scene, input, and animation.

  //! Module attachment (initialization).
  auto OnAttachedImpl(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;

  //! Shutdown cleanup.
  void OnShutdown() noexcept override;

protected:
  //! Clear backbuffer references (required by DemoModuleBase).
  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<renderer::CompositionView>& views) -> void override;

  //! Execute phase-specific work.
  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

private:
  friend class AsyncDemoVm;
  //! Setup functions (called once).
  auto SetupShaders() -> void;
  // Input actions/mappings for camera drone speed control

  //! Scene and rendering functions.
  auto EnsureExampleScene() -> void;
  auto EnsureMainCamera(int width, int height) -> void;
  // delta_time used to be absolute time; module now expects a per-frame
  // delta (seconds) for update integration. Use double precision here to
  // preserve granularity at high FPS.
  auto UpdateAnimations(double delta_time) -> void;
  auto UpdateSceneMutations(float delta_time) -> void;

  //! Ensures a spotlight exists as a child of the main camera node.
  auto EnsureCameraSpotLight() -> void;

  //! Debug overlay methods.

  auto TrackPhaseStart(const std::string& phase_name) -> void;
  auto TrackPhaseEnd() -> void;
  auto TrackFrameAction(const std::string& action) -> void;
  auto StartFrameTracking() -> void;
  auto EndFrameTracking() -> void;

  //! Dependencies (aggregated app context, non-owning).
  const DemoAppContext& app_;

  //! Scene and rendering.
  ActiveScene active_scene_;

  //! State tracking.
  bool initialized_ { false };
  std::chrono::steady_clock::time_point start_time_;
  // Last engine frame timestamp observed by this module. Used to compute
  // per-frame delta time for smooth integration of animations.
  std::chrono::steady_clock::time_point last_frame_time_;
  // Elapsed animation time in seconds since module start, computed from
  // engine frame timestamp for stable sampling across phases.
  double anim_time_ { 0.0 };

  // struct FrameActionTracker moved to AsyncDemoTypes.h

  FrameActionTracker current_frame_tracker_;
  std::vector<FrameActionTracker> frame_history_;
  static constexpr std::size_t kMaxFrameHistory = 60; // Keep 1 second at 60fps

  // Per-phase timing helpers
  std::chrono::steady_clock::time_point phase_start_time_;
  std::string current_phase_name_;

  // Per-sphere animation state (multiple spheres with different speeds)
  // struct SphereState moved to AsyncDemoTypes.h

  std::vector<SphereState> spheres_;

  //! Scene nodes for the example.
  scene::SceneNode multisubmesh_; // Per-submesh visibility/overrides
  scene::SceneNode main_camera_; // "MainCamera"
  scene::SceneNode camera_spot_light_; // Child of main camera

  //! Animation state (quad rotation removed; sphere orbits, camera fixed).
  int last_vis_toggle_ { -1 };
  int last_ovr_toggle_ { -1 };
  // Encapsulated camera drone state and behavior

  // Drone configuration
  auto ConfigureDrone() -> void;

  // Helper validation
  auto EnsureViewCameraRegistered() -> void;

  // Token for a registered platform pre-destroy callback; zero means none.
  size_t platform_window_destroy_handler_token_ { 0 };

  std::shared_ptr<AsyncDemoSettingsService> settings_service_;
  std::shared_ptr<AsyncDemoVm> vm_;
  std::shared_ptr<AsyncDemoPanel> async_panel_;
  // Hosted view
  ViewId main_view_id_ { kInvalidViewId };
  observer_ptr<ui::CameraRigController> last_camera_rig_;
  bool drone_configured_ { false };
};

} // namespace oxygen::examples::async
