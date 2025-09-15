//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Scene/Scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace oxygen {

class Platform;
class Graphics;

namespace platform {
  class Window;
} // namespace platform

namespace graphics {
  class Surface;
  class Framebuffer;
} // namespace graphics

namespace scene {
  class Scene;
  class SceneNode;
} // namespace scene

namespace engine {
  class FrameContext;
  class Renderer;
  struct RenderContext;
  class DepthPrePass;
  struct DepthPrePassConfig;
  class ShaderPass;
  struct ShaderPassConfig;
  using ModulePriority = NamedType<std::uint32_t, struct ModulePriorityTag>;
  using ModulePhaseMask = core::PhaseMask;
} // namespace engine

namespace examples::async {

  //! Next-generation graphics module demonstrating AsyncEngine integration.
  /*!
   This module replaces the legacy RenderController/RenderThread architecture
   with AsyncEngine's coroutine-based phase execution. It demonstrates:

   - Direct Graphics component usage instead of RenderController
   - Phase-based rendering coordination
   - Scene setup and animation in AsyncEngine framework
   - Command recording without RenderThread

   ### Architecture Notes

   - Inherits from EngineModule and registers for graphics rendering phases
   - Uses AsyncEngine's frame coordination instead of manual threading
   - Accesses Graphics components (Commander, DeferredReclaimer) directly
   - Demonstrates proper Graphics lifecycle integration

   @see AsyncEngine, EngineModule, Graphics
  */
  class MainModule final : public engine::EngineModule {
    OXYGEN_TYPED(MainModule)

  public:
    OXYGEN_MAKE_NON_COPYABLE(MainModule)
    OXYGEN_MAKE_NON_MOVABLE(MainModule)

    //! Constructor with Platform and Graphics dependencies.
    /*!
     @param platform Platform services for window management and events
     @param gfx_weak Weak reference to Graphics backend
     @param fullscreen Whether to create the window in full-screen mode
    */
    explicit MainModule(std::shared_ptr<Platform> platform,
      std::weak_ptr<Graphics> gfx_weak, bool fullscreen = false,
      observer_ptr<engine::Renderer> renderer = nullptr);

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

    //! Execute phase-specific work.
    auto OnFrameStart(engine::FrameContext& context) -> void override;
    auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
    auto OnTransformPropagation(engine::FrameContext& context)
      -> co::Co<> override;
    auto OnFrameGraph(engine::FrameContext& context) -> co::Co<> override;
    auto OnCommandRecord(engine::FrameContext& context) -> co::Co<> override;
    auto OnFrameEnd(engine::FrameContext& context) -> void override;

  private:
    //! Setup functions (called once).
    auto SetupCommandQueues() -> void;
    auto SetupMainWindow() -> void;
    auto SetupSurface() -> void;
    auto SetupRenderer() -> void;
    auto SetupFramebuffers() -> void;
    auto SetupShaders() -> void;

    //! Scene and rendering functions.
    auto EnsureExampleScene() -> void;
    auto EnsureMainCamera(int width, int height) -> void;
    // delta_time used to be absolute time; module now expects a per-frame
    // delta (seconds) for update integration. Use double precision here to
    // preserve granularity at high FPS.
    auto UpdateAnimations(double delta_time) -> void;
    auto UpdateSceneMutations(float delta_time) -> void;

    //! Frame graph setup (configure render passes).
    auto SetupRenderPasses() -> void;

    //! Command recording (execute render graph).
    auto ExecuteRenderCommands(engine::FrameContext& context) -> co::Co<>;

    //! Dependencies.
    std::shared_ptr<Platform> platform_;
    std::weak_ptr<Graphics> gfx_weak_;

    //! Configuration.
    bool fullscreen_;

    //! Graphics resources.
    std::weak_ptr<platform::Window> window_weak_;
    std::shared_ptr<graphics::Surface> surface_;
    std::vector<std::shared_ptr<graphics::Framebuffer>> framebuffers_;

    //! Scene and rendering.
    std::shared_ptr<scene::Scene> scene_;
    observer_ptr<engine::Renderer> renderer_;

    //! Render passes (configured during frame graph, executed during command
    //! record).
    std::shared_ptr<engine::DepthPrePass> depth_pass_;
    std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config_;
    std::shared_ptr<engine::ShaderPass> shader_pass_;
    std::shared_ptr<engine::ShaderPassConfig> shader_pass_config_;
    std::shared_ptr<engine::TransparentPass> transparent_pass_;
    std::shared_ptr<engine::TransparentPass::Config> transparent_pass_config_;

    //! State tracking.
    bool initialized_ { false };
    std::chrono::steady_clock::time_point start_time_;
    // Last engine frame timestamp observed by this module. Used to compute
    // per-frame delta time for smooth integration of animations.
    std::chrono::steady_clock::time_point last_frame_time_ {};
    // Elapsed animation time in seconds since module start, computed from
    // engine frame timestamp for stable sampling across phases.
    double anim_time_ { 0.0 };
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
  };

} // namespace examples::async
} // namespace oxygen
