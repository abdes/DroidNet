//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen {
class IAsyncEngine;
namespace control {
  class CommandDispatcher;
} // namespace control
namespace engine {
  class FrameContext;
} // namespace engine
namespace host {
  class SceneStage;
} // namespace host
} // namespace oxygen

namespace oxygen::examples {
class PanelCommands;
class CameraSettingsService;
class ContentSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {
class CameraVm;
class ContentVm;
class CameraRigController;
struct SceneEntry;
struct GenerationPublication;
} // namespace oxygen::examples::ui

namespace oxygen::examples {

//! Standard panel enablement settings for the demo shell.
struct DemoShellPanelConfig {
  bool content_loader { false };
  bool camera_controls { false };
  bool environment { false };
  bool diagnostics { false };
  bool post_process { false };
  bool ground_grid { true };
};

//! Configuration for the demo shell and its standard panels.
/*!
 Supplies the host bindings, the panels to show and the callbacks the
 panels run. `DemoModuleBase::MakeShellConfig()` fills the host bindings.

 @warning Callbacks must remain valid for the lifetime of the shell.
 @see DemoShell, DemoShell::Initialize
*/
struct DemoShellConfig {
  observer_ptr<IAsyncEngine> engine { nullptr };
  //! The host's scene stage; the shell follows the scene it publishes.
  observer_ptr<const host::SceneStage> stage { nullptr };
  //! Runs the commands posted by panel controls; null runs them at once.
  observer_ptr<control::CommandDispatcher> commands { nullptr };
  bool enable_camera_rig { true };
  bool force_environment_override { true };
  bool restore_environment_profile { false };
  SceneActivationPolicy scene_activation_policy {
    SceneActivationPolicy::kRestorePreferences,
  };
  std::optional<int> initial_environment_profile;
  std::string startup_skybox_path;
  std::optional<bool> initial_preview_sun_enabled;
  std::function<bool()> preview_scene_ready;

  ContentRootConfig content_roots {};
  DemoShellPanelConfig panel_config {};

  std::function<void(const ui::SceneEntry&)> on_scene_load_requested;
  std::function<void()> on_scene_load_cancel_requested;
  std::function<void()> on_clear_mounts;
  std::function<void(const std::filesystem::path&)> on_pak_mounted;
  std::function<void(const std::filesystem::path&)> on_loose_index_loaded;
  std::function<void(const ui::GenerationPublication&)> on_generation_published;
};

//! The demo panels, their services and the camera rig.
/*!
 The host owns the window, the scene stage, the toolbar and the console; the
 shell adds the side panels and the settings services behind them. Every
 panel control runs a command (see `PanelCommands`).

 `DemoModuleBase` drives the shell: frame start, the scene the stage
 publishes, the main view once published, the camera rig in `kInput`, and
 drawing after the host's toolbar.

 @see DemoShellUi, PanelRegistry, PanelCommands
*/
class DemoShell final {
public:
  DemoShell();
  ~DemoShell() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(DemoShell);
  OXYGEN_MAKE_NON_MOVABLE(DemoShell);

  //! Initialize the shell and register standard panels.
  auto Initialize(const DemoShellConfig& config) -> bool;

  //! Tick the camera rig and the content panel by the physical delta.
  auto Update(time::CanonicalDuration delta_time) -> void;

  //! Draw the side bar, the active panel and the axes below `top_inset`.
  auto Draw(observer_ptr<engine::FrameContext> fc, float top_inset) -> void;

  //! Follow the stage's scene and flush the services at frame start.
  auto OnFrameStart(const engine::FrameContext& context) -> void;

  //! Bind the services to the scene the stage holds, if it changed.
  auto SyncActiveScene() -> void;

  //! Notify the services once the main view is published.
  auto OnMainViewReady(ViewId view_id, const scene::SceneNode& camera,
    const ViewPort& viewport) -> void;

  //! Register a demo-specific panel with the shell.
  //!
  //! Panels can be registered before initialization; they will be deferred
  //! until the shell completes initialization and then forwarded to
  //! `DemoShellUi` for registration.
  auto RegisterPanel(std::shared_ptr<DemoPanel> panel) -> bool;

  //! The commands panel controls run; valid after initialization.
  [[nodiscard]] auto GetPanelCommands() const -> observer_ptr<PanelCommands>;

  //! Returns a non-owning pointer to the active scene (may be null).
  [[nodiscard]] auto TryGetScene() const -> observer_ptr<scene::Scene>;

  //! Cancel any in-flight content imports.
  auto CancelContentImport() -> void;

  //! Access the camera settings service for advanced control.
  [[nodiscard]] auto GetCameraSettingsService() -> CameraSettingsService&;

  //! Access the file browser service.
  [[nodiscard]] auto GetFileBrowserService() const -> FileBrowserService&;

  //! Access the camera rig controller (may be null if camera rig is disabled).
  [[nodiscard]] auto GetCameraRig() const
    -> observer_ptr<ui::CameraRigController>;

  //! Force an active panel by name (no-op if not registered).
  auto SetActivePanel(std::string_view panel_name) -> void;

  //! Returns the name of the currently active panel, if any.
  [[nodiscard]] auto GetActivePanelName() const -> std::optional<std::string>;

  //! Returns true when the main Vortex scene view should request height fog.
  [[nodiscard]] auto IsHeightFogPassRequested() const -> bool;

  //! Returns true when the main Vortex scene view should request local fog.
  [[nodiscard]] auto IsLocalFogPassRequested() const -> bool;

  //! Returns the content loader view model (may be null).
  [[nodiscard]] auto GetContentVm() const -> observer_ptr<ui::ContentVm>;

  //! Re-apply post-process settings to the active scene after hydration.
  auto ReapplyPostProcessSettingsToScene() -> void;

private:
  //! Completes initialization once required modules are available.
  auto CompleteInitialization() -> bool;
  auto SyncRuntimeState() -> void;
  auto OnSceneActivated(scene::Scene& scene) -> void;
  auto OnSceneCleared() -> void;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::examples
