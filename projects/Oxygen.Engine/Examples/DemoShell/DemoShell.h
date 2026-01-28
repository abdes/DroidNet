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
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/RenderingPanel.h"

namespace oxygen::engine {
class InputSystem;
class Renderer;
} // namespace oxygen::engine

namespace oxygen::examples {
class SkyboxService;
class FileBrowserService;
} // namespace oxygen::examples

namespace oxygen::examples {

//! Standard panel enablement settings for the demo shell.
struct DemoShellPanelConfig {
  bool content_loader { true };
  bool camera_controls { true };
  bool environment { true };
  bool lighting { true };
  bool rendering { true };
  bool settings { true };
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
  observer_ptr<engine::InputSystem> input_system { nullptr };
  std::shared_ptr<scene::Scene> scene { nullptr };
  std::filesystem::path cooked_root {};
  observer_ptr<FileBrowserService> file_browser_service { nullptr };
  observer_ptr<SkyboxService> skybox_service { nullptr };
  DemoShellPanelConfig panel_config {};
  bool enable_camera_rig { true };

  std::function<void(const data::AssetKey&)> on_scene_load_requested {};
  std::function<void(std::size_t)> on_dump_texture_memory {};
  std::function<std::optional<data::AssetKey>()> get_last_released_scene_key {};
  std::function<void()> on_force_trim {};
  std::function<void(const std::filesystem::path&)> on_pak_mounted {};
  std::function<void(const std::filesystem::path&)> on_loose_index_loaded {};

  std::function<observer_ptr<engine::Renderer>()> get_renderer {};
  std::function<ui::LightCullingDebugConfig()>
    get_light_culling_debug_config {};
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
  ~DemoShell();

  OXYGEN_MAKE_NON_COPYABLE(DemoShell);
  OXYGEN_MAKE_NON_MOVABLE(DemoShell);

  //! Initialize the shell and register standard panels.
  auto Initialize(const DemoShellConfig& config) -> bool;

  //! Update panel state or camera rig based on the frame phase.
  auto Update(time::CanonicalDuration delta_time) -> void;

  //! Draw the demo shell UI layout and active panel contents.
  auto Draw() -> void;

  //! Register a demo-specific panel with the shell.
  auto RegisterPanel(observer_ptr<DemoPanel> panel) -> bool;

  //! Update the active scene reference for panel and camera use.
  auto UpdateScene(std::shared_ptr<scene::Scene> scene) -> void;

  //! Set the active camera node used by the camera rig and panels.
  auto SetActiveCamera(scene::SceneNode camera) -> void;

  //! Update the skybox service used by environment panels.
  auto SetSkyboxService(observer_ptr<SkyboxService> skybox_service) -> void;

  //! Cancel any in-flight content imports.
  auto CancelContentImport() -> void;

  //! Access the camera lifecycle service for advanced control.
  [[nodiscard]] auto GetCameraLifecycle() -> CameraLifecycleService&;

  //! Get the current rendering view mode selection.
  [[nodiscard]] auto GetRenderingViewMode() const -> ui::RenderingViewMode;

private:
  auto InitializePanels() -> void;
  auto UpdatePanels() -> void;
  auto UpdateCameraControlPanelConfig() -> void;
  auto RegisterDemoPanels() -> void;

  struct Impl;
  std::unique_ptr<Impl> impl_ {};
};

} // namespace oxygen::examples
