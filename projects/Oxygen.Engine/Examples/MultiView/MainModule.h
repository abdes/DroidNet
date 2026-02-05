//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "MultiView/CompositingMode.h"
#include "MultiView/SceneBootstrapper.h"

namespace oxygen {
class AsyncEngine;
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {
class CommandRecorder;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::examples::ui {
class CameraRigController;
}

namespace oxygen::examples::multiview {

//! Multi-view rendering example demonstrating Phase 2 features.
/*!
 This example showcases multi-view rendering with:
 - Main view: Full-screen solid-shaded sphere
 - PiP view: Top-right corner (45% size)

 Both views render the same scene with different cameras.
 Demonstrates PrepareView/RenderView APIs and per-view state isolation.

 Integrates with DemoShell for architectural consistency but disables all
 standard panels since this demo has no interactive settings.
*/
class MainModule final : public oxygen::examples::DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::DemoModuleBase;

  explicit MainModule(const oxygen::examples::DemoAppContext& app,
    CompositingMode compositing_mode) noexcept;
  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MultiViewExample";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    constexpr int32_t kPriority = 500;
    return oxygen::engine::ModulePriority { kPriority };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kSceneMutation, core::PhaseId::kGameplay,
      core::PhaseId::kGuiUpdate, core::PhaseId::kPreRender,
      core::PhaseId::kCompositing, core::PhaseId::kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true; // Critical module
  }

  // EngineModule lifecycle
  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  void OnShutdown() noexcept override;

  // Example-specific setup
  auto HandleOnFrameStart(oxygen::engine::FrameContext& context)
    -> void override;

protected:
  // DemoModuleBase hooks
  auto BuildDefaultWindowProperties() const
    -> oxygen::platform::window::Properties override;
  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(oxygen::engine::FrameContext& context,
    std::vector<oxygen::examples::CompositionView>& views) -> void override;

  // EngineModule phase handlers
  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto OnSceneMutation(oxygen::engine::FrameContext& context)
    -> oxygen::co::Co<> override;
  auto OnGameplay(oxygen::engine::FrameContext& context)
    -> oxygen::co::Co<> override;
  auto OnGuiUpdate(oxygen::engine::FrameContext& context)
    -> oxygen::co::Co<> override;
  auto OnPreRender(oxygen::engine::FrameContext& context)
    -> oxygen::co::Co<> override;
  auto OnCompositing(oxygen::engine::FrameContext& context)
    -> oxygen::co::Co<> override;
  auto OnFrameEnd(oxygen::engine::FrameContext& context) -> void override;

private:
  auto UpdateCameras(const oxygen::platform::window::ExtentT& extent) -> void;

  const oxygen::examples::DemoAppContext& app_;
  SceneBootstrapper scene_bootstrapper_;

  // DemoShell integration (all panels disabled for this non-interactive demo)
  std::unique_ptr<oxygen::examples::DemoShell> shell_;

  oxygen::examples::ActiveScene active_scene_;

  // View identifiers
  ViewId main_view_id_ { kInvalidViewId };
  ViewId pip_view_id_ { kInvalidViewId };

  // Cameras
  oxygen::scene::SceneNode main_camera_node_;
  oxygen::scene::SceneNode pip_camera_node_;

  observer_ptr<oxygen::examples::ui::CameraRigController> last_camera_rig_ {
    nullptr
  };
  CompositingMode compositing_mode_ { CompositingMode::kBlend };
  oxygen::platform::window::ExtentT last_viewport_ { 0, 0 };
};

} // namespace oxygen::examples::multiview
