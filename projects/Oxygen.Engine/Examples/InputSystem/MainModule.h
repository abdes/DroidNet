//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/SingleViewModuleBase.h"

namespace oxygen::examples::input {

class MainModule : public SingleViewModuleBase {
  OXYGEN_TYPED(MainModule)
public:
  using Base = oxygen::examples::SingleViewModuleBase;

  explicit MainModule(const oxygen::examples::DemoAppContext& app);

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
      PhaseId::kGameplay, PhaseId::kGuiUpdate, PhaseId::kPreRender,
      PhaseId::kCompositing, PhaseId::kFrameEnd>();
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
  // Hook called by ExampleModuleBase::OnFrameStart for example-specific
  // setup like scene creation and context.SetScene.
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

protected:
private:
  auto InitInputBindings() noexcept -> bool;
  auto EnsureMainCamera(const int width, const int height) -> void;
  auto DrawDebugOverlay(engine::FrameContext& context) -> void;

  // The ExampleModuleBase provides `app_` and common window/render helpers.

  //! Scene and rendering.
  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode main_camera_; // "MainCamera"
  scene::SceneNode sphere_node_; // Sphere for jump animation

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

  // Demo mode: ground vs swimming
  bool swimming_mode_ { false };
  float swim_up_speed_ { 2.5f }; // units per second when Space is held
  // Defer sphere reset to a phase before transform propagation
  bool pending_ground_reset_ { false };
};

} // namespace oxygen::examples::input
