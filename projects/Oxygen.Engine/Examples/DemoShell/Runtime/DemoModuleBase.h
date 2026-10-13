//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DemoShell/DemoShell.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/OxCo/Co.h>

#ifdef OXYGEN_BUILD_UI_TESTS
struct ImGuiTestEngine;
#endif

namespace oxygen {
namespace console {
  class Console;
} // namespace console
namespace engine {
  class FrameContext;
} // namespace engine
namespace scene {
  class SceneNode;
} // namespace scene
} // namespace oxygen

namespace oxygen::examples {

#ifdef OXYGEN_BUILD_UI_TESTS
namespace testing {
  class UiTestSession;
} // namespace testing
#endif

//! Base class for the application module of a DemoShell example.
/*!
 The host owns the window, the views, the scene stage, the toolbar and the
 console. This base adds the DemoShell panels and services on top:

 - the shell sees every scene the stage publishes and the main view once it
   is published;
 - the camera rig and the content panel tick in `kInput` on the physical
   delta, so the camera keeps flying while the engine is paused;
 - the panels draw below the toolbar;
 - the console and palette window placement and the GPU work overlay's
   position persist in the demo settings, as the panels' settings do; the
   host's view mode, show flags and overlay switches are console variables
   saved in the application's archive;
 - a second instance of the demo runs without saving its settings.

 A derived module returns at least `kDemoPhases` from `GetSupportedPhases()`
 and calls the base implementation of every phase it overrides.
*/
class DemoModuleBase : public host::HostModule {
  OXYGEN_TYPED(DemoModuleBase)
public:
#ifdef OXYGEN_BUILD_UI_TESTS
  static constexpr auto kDemoPhases = kHostPhases
    | engine::MakeModuleMask<core::PhaseId::kInput, core::PhaseId::kFrameEnd>();
#else
  static constexpr auto kDemoPhases
    = kHostPhases | engine::MakeModuleMask<core::PhaseId::kInput>();
#endif

  explicit DemoModuleBase(const host::HostContext& host);
  ~DemoModuleBase() override;

  OXYGEN_MAKE_NON_COPYABLE(DemoModuleBase)
  OXYGEN_MAKE_NON_MOVABLE(DemoModuleBase)

  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnInput(observer_ptr<engine::FrameContext> context) -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
#ifdef OXYGEN_BUILD_UI_TESTS
  auto OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void override;
#endif

protected:
  //! Derived demos create and initialize the shell, starting from
  //! `MakeShellConfig()`.
  virtual auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> = 0;

  //! A shell configuration bound to the host: engine, scene stage and
  //! command dispatcher.
  [[nodiscard]] auto MakeShellConfig() -> DemoShellConfig;

  //! The shell; the module must be attached.
  [[nodiscard]] auto GetShell() -> DemoShell&;

  //! Draw the host's UI without the panels and make this binary's ImGui
  //! context current; true when an ImGui frame is open to draw into.
  [[nodiscard]] auto DrawHostUi(observer_ptr<engine::FrameContext> context)
    -> co::Co<bool>;

  auto OnScenePublished(engine::FrameContext& context) -> void override;
  auto OnMainViewReady(ViewId view_id, const scene::SceneNode& camera,
    const ViewPort& viewport) -> void override;

#ifdef OXYGEN_BUILD_UI_TESTS
  [[nodiscard]] auto UiTestOutputDirectory() const -> std::filesystem::path;
  virtual auto RegisterUiTests(ImGuiTestEngine* engine) -> void;
  auto StopUiTests() -> void;
#endif

private:
  auto OnHostAttached(observer_ptr<IAsyncEngine> engine) -> bool final;
  auto LoadUiPlacement() -> void;
  auto PersistUiPlacement() -> void;

  std::unique_ptr<DemoShell> shell_;
  std::optional<imgui::consoleui::WindowPlacement> saved_console_window_;
  std::optional<imgui::consoleui::WindowPlacement> saved_palette_window_;
  bool saved_auto_scroll_ { true };
  std::optional<host::GpuWorkPosition> saved_gpu_work_;
  std::array<bool, 3> saved_severity_filters_ { true, true, true };
#ifdef OXYGEN_BUILD_UI_TESTS
  std::unique_ptr<testing::UiTestSession> ui_tests_;
#endif
};

} // namespace oxygen::examples
