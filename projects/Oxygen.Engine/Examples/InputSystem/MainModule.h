//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "InputSystem/InputDebugPanel.h"

namespace oxygen::examples::ui {
class CameraRigController;
}

namespace oxygen::examples::input_system {

//! Main module for the InputSystem demo.
/*!
  Demonstrates the Oxygen InputSystem with actions, mappings, and triggers.
  Migrated to use DemoModuleBase and ForwardPipeline for rendering.

  @see DemoShell, DemoModuleBase
*/
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = DemoModuleBase;

  explicit MainModule(const DemoAppContext& app);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr engine::ModulePriority kPriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay,
      kGuiUpdate, kPreRender, kCompositing, kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true; // Critical module
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  // EngineModule lifecycle
  auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool override;
  void OnShutdown() noexcept override;

  // Example-specific setup
  auto HandleOnFrameStart(engine::FrameContext& context) -> void override;

protected:
  // DemoModuleBase hooks
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;
  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<CompositionView>& views) -> void override;

  // EngineModule phase handlers
  auto OnFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

private:
  auto InitInputBindings() noexcept -> bool;
  auto UpdateInputDebugPanelConfig() -> void;

  // Scene and rendering
  ActiveScene active_scene_;
  scene::SceneNode sphere_node_;

  // Hosted view
  ViewId main_view_id_ { kInvalidViewId };

  // Input actions
  std::shared_ptr<input::Action> shift_action_;
  std::shared_ptr<input::Action> jump_action_;
  std::shared_ptr<input::Action> jump_higher_action_;
  std::shared_ptr<input::Action> swim_up_action_;

  // Mapping contexts
  std::shared_ptr<input::InputMappingContext> modifier_keys_ctx_;
  std::shared_ptr<input::InputMappingContext> ground_movement_ctx_;
  std::shared_ptr<input::InputMappingContext> swimming_ctx_;

  // NOLINTBEGIN(*-magic-numbers)
  // Simple physics (Z is up in Oxygen)
  glm::vec3 sphere_base_pos_ { 0.0F, -5.0F, 0.0F }; // Forward along -Y
  bool sphere_in_air_ { false };
  float sphere_vel_z_ { 0.0F };
  float gravity_ { -9.81F };
  float jump_impulse_ { 4.5F };
  float jump_higher_impulse_ { 7.0F };

  // Demo mode
  bool swimming_mode_ { false };
  float swim_up_speed_ { 2.5F };
  bool pending_ground_reset_ { false };
  // NOLINTEND(*-magic-numbers)

  // DemoShell and panels
  std::unique_ptr<DemoShell> shell_;
  std::shared_ptr<InputDebugPanel> input_debug_panel_;

  observer_ptr<ui::CameraRigController> last_camera_rig_ { nullptr };
};

} // namespace oxygen::examples::input_system
