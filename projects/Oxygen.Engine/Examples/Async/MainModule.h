//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include "Async/AsyncScene.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::async {

class AsyncDemoPanel;
class AsyncDemoVm;
class AsyncDemoSettingsService;
class AsyncShowcase;

//! Async frame phases, animated LODs/materials and camera lighting on Vortex.
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = DemoModuleBase;
  explicit MainModule(const host::HostContext& host);
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
    return kDemoPhases | engine::MakeModuleMask<core::PhaseId::kGameplay>();
  }
  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true;
  }
  auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  auto OnShutdown() noexcept -> void override;

  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  auto OnScenePublished(engine::FrameContext& context) -> void override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;
#ifdef OXYGEN_BUILD_UI_TESTS
  auto RegisterUiTests(ImGuiTestEngine* engine) -> void override;
#endif

private:
  auto EnsureExampleScene() -> void;
  auto EnsureMainCamera(int width, int height) -> void;
  auto EnsureCameraSpotLight() -> void;
  auto ConfigureDrone(DemoShell& shell) -> void;
  [[nodiscard]] auto ResolveViewExtent() const noexcept -> glm::uvec2;

  host::ActiveScene active_scene_;
  double anim_time_ { 0.0 };
  AsyncScene scene_content_;
  std::unique_ptr<AsyncShowcase> showcase_;
  scene::SceneNode main_camera_;
  scene::SceneNode camera_spot_light_;
  scene::SceneNode sun_light_;
  std::shared_ptr<AsyncDemoSettingsService> settings_service_;
  std::shared_ptr<AsyncDemoVm> vm_;
  std::shared_ptr<AsyncDemoPanel> async_panel_;
  ViewId main_view_id_ { kInvalidViewId };
};

} // namespace oxygen::examples::async
