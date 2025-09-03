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
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/DepthPrePass.h>
#include <Oxygen/Renderer/ShaderPass.h>
#include <Oxygen/Scene/Scene.h>

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
      std::weak_ptr<Graphics> gfx_weak, bool fullscreen = false);

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
    auto UpdateAnimations(float time_seconds) -> void;
    auto UpdateSceneMutations(float time_seconds) -> void;

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
    std::shared_ptr<engine::Renderer> renderer_;

    //! Render passes (configured during frame graph, executed during command
    //! record).
    std::shared_ptr<engine::DepthPrePass> depth_pass_;
    std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config_;
    std::shared_ptr<engine::ShaderPass> shader_pass_;
    std::shared_ptr<engine::ShaderPassConfig> shader_pass_config_;

    //! State tracking.
    bool initialized_ { false };
    float start_time_ { 0.0f };

    //! Scene nodes for the example.
    scene::SceneNode sphere_distance_; // LOD policy: Distance
    scene::SceneNode multisubmesh_; // Per-submesh visibility/overrides
    scene::SceneNode main_camera_; // "MainCamera"

    //! Animation state.
    float quad_rotation_angle_ { 0.0f };
    int last_vis_toggle_ { -1 };
    int last_ovr_toggle_ { -1 };
  };

} // namespace examples::async
} // namespace oxygen
