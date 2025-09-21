//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Scene/Scene.h>
#include <glm/glm.hpp>

#include "AsyncEngineApp.h"

namespace oxygen::input {
class Action;
class InputMappingContext;
} // namespace oxygen::input

namespace oxygen::platform {
class Window;
} // namespace oxygen::platform

namespace oxygen::graphics {
class Surface;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::engine {
class FrameContext;
} // namespace oxygen::engine

namespace oxygen::engine::examples {

class MainModule : public oxygen::engine::EngineModule {
  OXYGEN_TYPED(MainModule)
public:
  using Base = oxygen::engine::EngineModule;

  explicit MainModule(const AsyncEngineApp& app);

  // EngineModule metadata overrides
  // -------------------------------------------------
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    return engine::ModulePriority { 500 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    using namespace core;
    return engine::MakeModuleMask<PhaseId::kFrameStart, PhaseId::kSceneMutation,
      PhaseId::kGameplay, PhaseId::kGuiUpdate, PhaseId::kFrameGraph,
      PhaseId::kCommandRecord, PhaseId::kFrameEnd>();
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  // EngineModule lifecycle
  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  void OnShutdown() noexcept override;

  // EngineModule phase handlers we participate in
  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameGraph(engine::FrameContext& context) -> co::Co<> override;
  auto OnCommandRecord(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

private:
  auto InitInputBindings() noexcept -> bool;
  auto SetupMainWindow() -> bool;
  auto SetupSurface() -> bool;
  auto SetupFramebuffers() -> bool;
  auto SetupRenderPasses() -> void;

  auto EnsureMainCamera(const int width, const int height) -> void;

  auto DrawDebugOverlay(engine::FrameContext& context) -> void;

  const AsyncEngineApp& app_;

  //! Scene and rendering.
  engine::RenderContext render_context_;
  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode main_camera_; // "MainCamera"
  scene::SceneNode sphere_node_; // Sphere for jump animation

  //! Render passes (configured during frame graph, executed during command
  //! record).
  std::shared_ptr<engine::DepthPrePass> depth_pass_;
  std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config_;
  std::shared_ptr<engine::ShaderPass> shader_pass_;
  std::shared_ptr<engine::ShaderPassConfig> shader_pass_config_;
  std::shared_ptr<engine::TransparentPass> transparent_pass_;
  std::shared_ptr<engine::TransparentPass::Config> transparent_pass_config_;

  std::weak_ptr<platform::Window> window_weak_;
  std::shared_ptr<graphics::Surface> surface_;
  std::vector<std::shared_ptr<graphics::Framebuffer>> framebuffers_;

  // Stored actions for querying state later during frames
  std::shared_ptr<oxygen::input::Action> shift_action_;
  std::shared_ptr<oxygen::input::Action> jump_action_;
  std::shared_ptr<oxygen::input::Action> jump_higher_action_;
  std::shared_ptr<oxygen::input::Action> swim_up_action_;
  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action>
    left_mouse_action_; // helper for chains
  std::shared_ptr<oxygen::input::Action> pan_action_; // Axis2D mouse pan

  // Mapping contexts we may toggle/inspect later
  std::shared_ptr<oxygen::input::InputMappingContext> modifier_keys_ctx_;
  std::shared_ptr<oxygen::input::InputMappingContext> ground_movement_ctx_;
  std::shared_ptr<oxygen::input::InputMappingContext> swimming_ctx_;
  std::shared_ptr<oxygen::input::InputMappingContext> camera_controls_ctx_;

  // Simple physics and camera controls
  glm::vec3 sphere_base_pos_ { 0.0f, 0.0f, -2.0f };
  bool sphere_in_air_ { false };
  float sphere_vel_y_ { 0.0f };
  float gravity_ { -9.81f };
  float jump_impulse_ { 4.5f };
  float jump_higher_impulse_ { 7.0f };
  float zoom_step_ { 0.75f };
  float min_cam_distance_ { 0.75f };
  float max_cam_distance_ { 40.0f };
  float pan_sensitivity_ { 0.005f }; // world units per pixel
  std::chrono::steady_clock::time_point last_frame_time_ {};
};

} // namespace oxygen::engine::examples
