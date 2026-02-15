//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>

namespace oxygen {
class AsyncEngine;
namespace engine {
  class FrameContext;
  struct ShaderPassConfig;
  struct LightCullingPassConfig;
}
namespace imgui {
  class ImGuiModule;
}
namespace renderer {
  class RenderingPipeline;
}
} // namespace oxygen

namespace oxygen::examples {

class PanelRegistry;
class DemoPanel;
class UiSettingsService;
class RenderingSettingsService;
class LightCullingSettingsService;
class CameraSettingsService;
class ContentSettingsService;
class EnvironmentSettingsService;
class GridSettingsService;
class FileBrowserService;
class RenderingPipeline;
struct DemoShellPanelConfig;

namespace ui {

  class RenderingVm;
  class LightCullingVm;
  class CameraVm;
  class ContentVm;
  class EnvironmentVm;
  class PostProcessSettingsService;
  class CameraRigController;
  class GridVm;

  //! UI shell hosting the side bar and side panel.
  /*!
   Provides a reusable UI layout for demos, consisting of a left-docked
   `PanelSideBar` and a `SidePanel` hosting a single active panel.

   Also owns the ViewModels for rendering and lighting panels, creating them
   lazily when the pass configs become available.
  */
  class DemoShellUi {
  public:
    DemoShellUi(observer_ptr<AsyncEngine> engine,
      observer_ptr<PanelRegistry> panel_registry,
      observer_ptr<UiSettingsService> ui_settings_service,
      observer_ptr<RenderingSettingsService> rendering_settings_service,
      observer_ptr<LightCullingSettingsService> light_culling_settings_service,
      observer_ptr<CameraSettingsService> camera_settings_service,
      observer_ptr<ContentSettingsService> content_settings_service,
      observer_ptr<EnvironmentSettingsService> environment_settings_service,
      observer_ptr<PostProcessSettingsService> post_process_settings_service,
      observer_ptr<GridSettingsService> grid_settings_service,
      observer_ptr<CameraRigController> camera_rig,
      observer_ptr<FileBrowserService> file_browser_service,
      const DemoShellPanelConfig& panel_config);
    ~DemoShellUi();

    OXYGEN_MAKE_NON_COPYABLE(DemoShellUi)
    OXYGEN_DEFAULT_MOVABLE(DemoShellUi)

    //! Draws the side bar and side panel.
    auto Draw(observer_ptr<engine::FrameContext> fc) -> void;

    //! Ensures rendering panel is created when pass config is available.
    auto EnsureRenderingPanelReady(renderer::RenderingPipeline& pipeline)
      -> void;

    //! Ensures lighting panel is created when pass configs are available.
    auto EnsureLightingPanelReady(renderer::RenderingPipeline& pipeline)
      -> void;

    //! Registers a custom panel with the shared panel registry.
    auto RegisterCustomPanel(std::shared_ptr<DemoPanel> panel) -> bool;

    //! Returns the rendering view model (may be null if not yet created).
    [[nodiscard]] auto GetRenderingVm() const -> observer_ptr<RenderingVm>;

    //! Returns the light culling view model (may be null if not yet created).
    [[nodiscard]] auto GetLightCullingVm() const
      -> observer_ptr<LightCullingVm>;

    //! Returns the camera view model.
    [[nodiscard]] auto GetCameraVm() const -> observer_ptr<CameraVm>;

    //! Returns the content view model.
    [[nodiscard]] auto GetContentVm() const -> observer_ptr<ContentVm>;

    //! Returns the environment view model.
    [[nodiscard]] auto GetEnvironmentVm() const -> observer_ptr<EnvironmentVm>;

    //! Returns the last mouse-down position captured by the UI.
    [[nodiscard]] auto GetLastMouseDownPosition() const
      -> std::optional<SubPixelPosition>;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::optional<SubPixelPosition> last_mouse_down_position_;
  };

} // namespace ui

} // namespace oxygen::examples
