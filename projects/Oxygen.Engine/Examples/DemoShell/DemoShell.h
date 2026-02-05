//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/DemoPanel.h"

namespace oxygen {
class AsyncEngine;
namespace engine {
  class FrameContext;
  class InputSystem;
  class Renderer;
  struct ShaderPassConfig;
  struct LightCullingPassConfig;
} // namespace oxygen::engine
}

namespace oxygen::examples {
class SkyboxService;
class CameraSettingsService;
class ContentSettingsService;
class RenderingPipeline;
} // namespace oxygen::examples

namespace oxygen::examples::ui {
class CameraVm;
class ContentVm;
class CameraRigController;
struct SceneEntry;
} // namespace oxygen::examples::ui

namespace oxygen::examples {

//! Standard panel enablement settings for the demo shell.
struct DemoShellPanelConfig {
  bool content_loader { false };
  bool camera_controls { false };
  bool environment { false };
  bool lighting { false };
  bool rendering { false };
  bool post_process { false };
};

//! Configuration for the demo shell and its standard panels.
/*!
 Supplies the dependencies and callbacks needed to initialize the demo shell
 panels and orchestrate runtime updates.

### Key Features

- **Centralized Wiring**: Connects UI panels to engine services and callbacks.
- **Safe Ownership**: Uses non-owning pointers for engine dependencies.
- **Flexible Updates**: Allows dynamic renderer and render-graph bindings.

### Usage Patterns

 Construct once during module attachment and pass it to
 `DemoShell::Initialize`.

### Architecture Notes

 The configuration favors non-owning references to avoid lifetime coupling.
 Panels capture callbacks for scene loading and renderer integration.

 @warning Callbacks must remain valid for the lifetime of the shell.
 @see DemoShell, DemoShell::Initialize
*/
struct DemoShellConfig {
  observer_ptr<AsyncEngine> engine { nullptr };
  bool enable_camera_rig { true };

  ContentRootConfig content_roots {};
  DemoShellPanelConfig panel_config {};

  std::function<void(const ui::SceneEntry&)> on_scene_load_requested;
  std::function<void()> on_scene_load_cancel_requested;
  std::function<void(std::size_t)> on_dump_texture_memory;
  std::function<std::optional<data::AssetKey>()> get_last_released_scene_key;
  std::function<void()> on_force_trim;
  std::function<void(const std::filesystem::path&)> on_pak_mounted;
  std::function<void(const std::filesystem::path&)> on_loose_index_loaded;

  std::function<observer_ptr<RenderingPipeline>()> get_active_pipeline;
};

//! Orchestrates the demo shell UI, panels, and camera helpers.
/*!
 Manages panel initialization, registration, and per-frame updates for demo
 applications. The shell owns the common debug panels and provides a
 consistent layout through `DemoShellUi`.

### Key Features

- **Standard Panels**: Content loading, camera controls, lighting, rendering,
  settings, and environment debugging.
- **Camera Integration**: Wires a camera rig to the UI and lifecycle service.
- **Centralized Draw**: Owns the DemoShell UI layout and draw sequencing.

### Usage Patterns

 Create the shell during module attachment and drive it from scene mutation,
 gameplay, and GUI update phases.

### Architecture Notes

 The shell does not own engine systems; it receives them through the config
 and forwards them to each panel.

 @see DemoShellUi, PanelRegistry, CameraLifecycleService
*/
class DemoShell final {
public:
  DemoShell();
  ~DemoShell() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(DemoShell);
  OXYGEN_MAKE_NON_MOVABLE(DemoShell);

  //! Initialize the shell and register standard panels.
  auto Initialize(const DemoShellConfig& config) -> bool;

  //! Update panel state or camera rig based on the frame phase.
  auto Update(time::CanonicalDuration delta_time) -> void;

  //! Draw the demo shell UI layout and active panel contents.
  auto Draw(engine::FrameContext& fc) -> void;

  //! Synchronize panel-driven settings during the scene-mutation phase.
  auto SyncPanels() -> void;

  //! Register a demo-specific panel with the shell.
  //!
  //! Panels can be registered before initialization; they will be deferred
  //! until the shell completes initialization and then forwarded to
  //! `DemoShellUi` for registration.
  auto RegisterPanel(std::shared_ptr<DemoPanel> panel) -> bool;

  //! Set the active scene (ownership transferred to the shell).
  auto SetScene(std::unique_ptr<scene::Scene> scene) -> ActiveScene;

  //! Returns a value object for accessing the current active scene.
  [[nodiscard]] auto GetActiveScene() const -> ActiveScene;

  //! Returns a non-owning pointer to the active scene (may be null).
  [[nodiscard]] auto TryGetScene() const -> observer_ptr<scene::Scene>;

  //! Set the active camera node used by the camera rig and panels.
  auto SetActiveCamera(scene::SceneNode camera) -> void;

  //! Returns the skybox service used by environment panels (created on demand).
  [[nodiscard]] auto GetSkyboxService() -> observer_ptr<SkyboxService>;

  //! Cancel any in-flight content imports.
  auto CancelContentImport() -> void;

  //! Access the camera lifecycle service for advanced control.
  [[nodiscard]] auto GetCameraLifecycle() -> CameraLifecycleService&;

  //! Access the file browser service.
  [[nodiscard]] auto GetFileBrowserService() const -> FileBrowserService&;

  //! Access the camera rig controller (may be null if camera rig is disabled).
  [[nodiscard]] auto GetCameraRig() const
    -> observer_ptr<ui::CameraRigController>;

  //! Get the current rendering view mode selection.
  [[nodiscard]] auto GetRenderingViewMode() const -> RenderMode;

  //! Force an active panel by name (no-op if not registered).
  auto SetActivePanel(std::string_view panel_name) -> void;

  //! Returns the name of the currently active panel, if any.
  [[nodiscard]] auto GetActivePanelName() const -> std::optional<std::string>;

  //! Returns the current rendering debug mode.
  [[nodiscard]] auto GetRenderingDebugMode() const -> engine::ShaderDebugMode;

  //! Returns the current wireframe color.
  [[nodiscard]] auto GetRenderingWireframeColor() const -> graphics::Color;

  //! Returns the current light culling visualization mode.
  [[nodiscard]] auto GetLightCullingVisualizationMode() const
    -> engine::ShaderDebugMode;

  //! Returns the content loader view model (may be null).
  [[nodiscard]] auto GetContentVm() const -> observer_ptr<ui::ContentVm>;

private:
  //! Completes initialization once required modules are available.
  auto CompleteInitialization() -> bool;
  auto UpdatePanels() -> void;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::examples
