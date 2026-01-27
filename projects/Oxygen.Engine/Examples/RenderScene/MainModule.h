//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "Common/AsyncEngineApp.h"
#include "Common/SingleViewExample.h"
#include "Common/SkyboxManager.h"
#include "DemoShell/DemoShellUi.h"
#include "RenderScene/FlyCameraController.h"
#include "RenderScene/OrbitCameraController.h"
#include "RenderScene/UI/AxesWidget.h"
#include "RenderScene/UI/CameraControlPanel.h"
#include "RenderScene/UI/ContentLoaderPanel.h"
#include "RenderScene/UI/EnvironmentDebugPanel.h"
#include "RenderScene/UI/LightCullingDebugPanel.h"

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::content {
class PakFile;
class LooseCookedInspection;
} // namespace oxygen::content

namespace oxygen::content::import {
class AssetImporter;
} // namespace oxygen::content::import

namespace oxygen::examples::demo_shell {
class DemoPanel;
} // namespace oxygen::examples::demo_shell

namespace oxygen::examples::render_scene {

class SceneLoader;

class MainModule final : public common::SingleViewExample {
  OXYGEN_TYPED(MainModule)
public:
  using Base = oxygen::examples::common::SingleViewExample;

  explicit MainModule(const oxygen::examples::common::AsyncEngineApp& app);

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

  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  struct PendingSceneSwap {
    std::shared_ptr<scene::Scene> scene;
    scene::SceneNode active_camera;
    data::AssetKey scene_key {};
  };

  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  void OnShutdown() noexcept override;

  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

private:
  auto InitInputBindings() noexcept -> bool;
  auto InitializeUIPanels() -> void;
  auto UpdateCameraControlPanelConfig() -> void;
  auto EnsureFallbackCamera(const int width, const int height) -> void;
  auto EnsureActiveCameraViewport(const int width, const int height) -> void;
  auto ApplyOrbitAndZoom(time::CanonicalDuration delta_time) -> void;
  auto EnsureViewCameraRegistered() -> void;
  auto ReleaseCurrentSceneAsset(const char* reason) -> void;

  auto UpdateUIPanels() -> void;
  auto DrawUI() -> void;
  auto ApplyRenderModeFromKnobs() -> void;
  auto SyncCameraModeFromKnobs() -> void;
  auto RegisterDemoPanels() -> void;
  auto ClearSceneRuntime(const char* reason) -> void;

  // Scene and rendering.
  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode active_camera_;
  scene::NodeHandle registered_view_camera_ {};

  std::optional<PendingSceneSwap> pending_scene_swap_;
  std::shared_ptr<SceneLoader> scene_loader_;
  std::unique_ptr<common::SkyboxManager> skybox_manager_;
  scene::Scene* skybox_manager_scene_ { nullptr };

  // Camera input.
  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action> rmb_action_;
  std::shared_ptr<oxygen::input::Action> orbit_action_;
  std::shared_ptr<oxygen::input::Action> move_fwd_action_;
  std::shared_ptr<oxygen::input::Action> move_bwd_action_;
  std::shared_ptr<oxygen::input::Action> move_left_action_;
  std::shared_ptr<oxygen::input::Action> move_right_action_;
  std::shared_ptr<oxygen::input::Action> move_up_action_;
  std::shared_ptr<oxygen::input::Action> move_down_action_;
  std::shared_ptr<oxygen::input::Action> fly_plane_lock_action_;
  std::shared_ptr<oxygen::input::Action> fly_boost_action_;
  std::shared_ptr<oxygen::input::InputMappingContext> orbit_controls_ctx_;
  std::shared_ptr<oxygen::input::InputMappingContext> fly_controls_ctx_;

  enum class CameraMode { kOrbit, kFly };
  CameraMode camera_mode_ { CameraMode::kFly };
  std::unique_ptr<OrbitCameraController> orbit_controller_;
  std::unique_ptr<FlyCameraController> fly_controller_;

  auto UpdateActiveCameraInputContext() -> void;
  auto ResetCameraToInitialPose() -> void;

  // UI panels
  ui::ContentLoaderPanel content_loader_panel_;
  ui::CameraControlPanel camera_control_panel_;
  ui::LightCullingDebugPanel light_culling_debug_panel_;
  ui::EnvironmentDebugPanel environment_debug_panel_;
  ui::AxesWidget axes_widget_;

  //! Demo shell UI state and registration.
  /*!
   Lifetime guarantees for the demo shell:

   - **Panels**: Stored in `demo_panels_` and registered once via
     `RegisterDemoPanels()`. The registry stores non-owning pointers and assumes
     the panel objects live for the lifetime of `MainModule`.
   - **Knobs**: `demo_knobs_` is owned by `MainModule` and referenced by the
     toolbar via `observer_ptr`, so updates are immediate and stable.
   - **UI Shell**: `demo_shell_ui_` does not own panels; it only orchestrates
     layout and draws through the registry each frame.
  */
  demo_shell::DemoKnobsViewModel demo_knobs_ {};
  demo_shell::PanelRegistry panel_registry_ {};
  demo_shell::DemoShellUi demo_shell_ui_ {};
  std::vector<std::unique_ptr<demo_shell::DemoPanel>> demo_panels_ {};

  // Content and scene state
  std::filesystem::path content_root_;
  bool pending_load_scene_ { false };
  std::optional<data::AssetKey> pending_scene_key_;
  std::optional<data::AssetKey> current_scene_key_;
  std::optional<data::AssetKey> last_released_scene_key_;
  glm::vec3 initial_camera_position_ { 0.0F, -15.0F, 0.0F };
  glm::vec3 initial_camera_target_ { 0.0F, 0.0F, 0.0F };
  glm::quat initial_camera_rotation_ { 1.0F, 0.0F, 0.0F, 0.0F };

  int last_viewport_w_ { 0 };
  int last_viewport_h_ { 0 };

  // Debug/instrumentation.
  bool logged_gameplay_tick_ { false };
  bool was_orbiting_last_frame_ { false };

  bool pending_sync_active_camera_ { false };
  bool pending_reset_camera_ { false };
};

} // namespace oxygen::examples::render_scene
