//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Host/MainViewOptions.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Host/ViewPublisher.h>
#include <Oxygen/Host/ViewportToolbar.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/ImGui/Console/ConsoleHost.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen {
class IAsyncEngine;
namespace console {
  class Console;
} // namespace console
namespace engine {
  class FrameContext;
} // namespace engine
namespace imgui {
  class ImGuiModule;
} // namespace imgui
namespace scene {
  class SceneNode;
} // namespace scene
} // namespace oxygen

namespace oxygen::host {

struct HostContext;
class HostWindow;

//! The views a host module renders this frame.
struct HostViews {
  std::vector<vortex::CompositionView> views;
  //! The main game view, when there is one.
  std::optional<ViewId> main;
};

//! Base for the application module that owns the window, the views and the
//! scene hand-off.
/*!
 Creates the host window on attach (unless headless) and runs the window,
 the view publisher and the scene stage across four phases:

 - `kFrameStart`: register the window surface and apply resizes, publish a
   staged scene, then set the frame's scene.
 - `kSceneMutation`: collect this frame's views from `BuildViews`, then apply
   the `host.view_mode` and `host.show.*` console variables to the main view.
 - `kGuiUpdate`: unless headless, draw the console, the command palette, the
   viewport toolbar and the stats overlay.
 - `kPublishViews`: publish the scene views.
 - `kCompositing`: composite them onto the window.

 A subclass returns `GetSupportedPhases()` with at least `kHostPhases`, and
 calls the base implementation when it overrides `OnFrameStart`,
 `OnSceneMutation`, `OnGuiUpdate`, `OnShutdown`, `RegisterConsoleBindings`,
 `RegisterCommands` or `ApplyConsoleCVars`.

 Every interaction in the debug UI (toolbar, hotkeys, console, palette) is
 posted to the host's command dispatcher as a console line.
*/
class HostModule : public engine::EngineModule {
  OXYGEN_TYPED(HostModule)

public:
  static constexpr auto kHostPhases
    = engine::MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kSceneMutation, core::PhaseId::kGuiUpdate,
      core::PhaseId::kPublishViews, core::PhaseId::kCompositing>();

  //! `host` must outlive the module.
  OXGN_HOST_API explicit HostModule(const HostContext& host);
  OXGN_HOST_API ~HostModule() override;

  OXYGEN_MAKE_NON_COPYABLE(HostModule)
  OXYGEN_MAKE_NON_MOVABLE(HostModule)

  OXGN_HOST_NDAPI auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool final;
  OXGN_HOST_API auto OnShutdown() noexcept -> void override;

  OXGN_HOST_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_HOST_API auto RegisterCommands(
    observer_ptr<commands::CommandRegistry> registry) noexcept -> void override;
  OXGN_HOST_API auto ApplyConsoleCVars(
    observer_ptr<const console::Console> console) noexcept -> void override;

  OXGN_HOST_API auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  OXGN_HOST_API auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_HOST_API auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_HOST_API auto OnPublishViews(observer_ptr<engine::FrameContext> context)
    -> co::Co<> final;
  OXGN_HOST_API auto OnCompositing(observer_ptr<engine::FrameContext> context)
    -> co::Co<> final;

protected:
  //! Attach-time work after the window exists; false fails the attach.
  //! Exceptions are logged and fail the attach.
  virtual auto OnHostAttached(observer_ptr<IAsyncEngine> /*engine*/) -> bool
  {
    return true;
  }

  //! The main window's title, size and flags.
  OXGN_HOST_NDAPI virtual auto BuildWindowProperties() const
    -> platform::window::Properties;

  //! Fill `views` with the views to render this frame.
  virtual auto BuildViews(engine::FrameContext& context, HostViews& views)
    -> void = 0;

  //! Called at frame start right after a staged scene became active.
  virtual auto OnScenePublished(engine::FrameContext& /*context*/) -> void { }

  //! Called once the main view is published.
  virtual auto OnMainViewReady(ViewId /*view_id*/,
    const scene::SceneNode& /*camera*/, const ViewPort& /*viewport*/) -> void
  {
  }

  //! Add texture layers after the scene layers.
  virtual auto AppendCompositionLayers(engine::FrameContext& /*context*/,
    vortex::Renderer::RuntimeCompositionInput& /*input*/) -> void
  {
  }

  //! Keep the history of a view that is absent this frame.
  [[nodiscard]] virtual auto RetainInactiveView(
    ViewId /*view_id*/) const noexcept -> bool
  {
    return false;
  }

  [[nodiscard]] auto Host() const noexcept -> const HostContext&
  {
    return host_;
  }
  //! Null when headless.
  [[nodiscard]] auto Window() const noexcept -> observer_ptr<HostWindow>
  {
    return observer_ptr { window_.get() };
  }
  [[nodiscard]] auto Stage() noexcept -> SceneStage& { return stage_; }
  //! The main-view settings applied this frame.
  [[nodiscard]] auto GetMainViewOptions() const noexcept
    -> const MainViewOptions&
  {
    return main_view_options_;
  }
  [[nodiscard]] auto Publisher() noexcept -> ViewPublisher&
  {
    return publisher_;
  }

  //! True when headless, or while the window is open and not closing.
  OXGN_HOST_NDAPI auto HasRenderableWindow() const noexcept -> bool;

  OXGN_HOST_NDAPI auto ResolveRenderer() const noexcept
    -> observer_ptr<vortex::Renderer>;
  //! Null when headless.
  OXGN_HOST_NDAPI auto ResolveImGuiModule() const noexcept
    -> observer_ptr<imgui::ImGuiModule>;

  //! Posts a console line to the command dispatcher.
  OXGN_HOST_API auto PostLine(
    std::string line, commands::CommandSource source) const -> void;

private:
  const HostContext& host_;
  std::shared_ptr<HostWindow> window_;
  ViewPublisher publisher_;
  SceneStage stage_;
  HostViews views_;
  MainViewOptions main_view_options_;
  imgui::consoleui::ConsoleHost console_host_;
  ViewportToolbar toolbar_;
};

} // namespace oxygen::host
