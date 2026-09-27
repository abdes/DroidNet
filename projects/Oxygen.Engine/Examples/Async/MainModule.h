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

#include "Async/AsyncDemoTypes.h"
#include "Async/AsyncScene.h"
#include "DemoShell/ActiveScene.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {
class CameraRigController;
}

namespace oxygen::examples::async {

class AsyncDemoPanel;
class AsyncDemoVm;
class AsyncDemoSettingsService;

//! Async frame phases, animated LODs/materials and camera lighting on Vortex.
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = DemoModuleBase;
  explicit MainModule(const DemoAppContext& app);
  ~MainModule() override;
  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "AsyncDemo";
  }
  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr auto kDemoPriority = engine::ModulePriority { 500 };
    return kDemoPriority;
  }
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay,
      kPublishViews, kGuiUpdate, kPreRender, kCompositing, kFrameEnd>();
  }
  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true;
  }
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;
  auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  auto OnShutdown() noexcept -> void override;

protected:
  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<vortex::CompositionView>& views) -> void override;
  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPublishViews(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
#ifdef OXYGEN_BUILD_UI_TESTS
  auto RegisterUiTests(ImGuiTestEngine* engine) -> void override;
#endif

private:
  auto EnsureExampleScene() -> void;
  auto EnsureMainCamera(int width, int height) -> void;
  auto EnsureCameraSpotLight() -> void;
  auto ConfigureDrone() -> void;
  [[nodiscard]] auto ResolveViewExtent() const noexcept -> glm::uvec2;

  auto TrackPhaseStart(const std::string& phase_name) -> void;
  auto TrackPhaseEnd() -> void;
  auto TrackFrameAction(const std::string& action) -> void;
  auto StartFrameTracking() -> void;
  auto EndFrameTracking() -> void;

  ActiveScene active_scene_;
  double anim_time_ { 0.0 };
  FrameActionTracker current_frame_tracker_;
  FrameActionTracker completed_frame_tracker_;
  std::chrono::steady_clock::time_point phase_start_time_;
  std::string current_phase_name_;
  AsyncScene scene_content_;
  scene::SceneNode main_camera_;
  scene::SceneNode camera_spot_light_;
  scene::SceneNode sun_light_;
  std::shared_ptr<AsyncDemoSettingsService> settings_service_;
  std::shared_ptr<AsyncDemoVm> vm_;
  std::shared_ptr<AsyncDemoPanel> async_panel_;
  ViewId main_view_id_ { kInvalidViewId };
  observer_ptr<ui::CameraRigController> last_camera_rig_;
  bool drone_configured_ { false };
};

} // namespace oxygen::examples::async
