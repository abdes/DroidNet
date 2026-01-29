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
#include <Oxygen/OxCo/Co.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "MultiView/CompositorGraph.h"
#include "MultiView/DemoView.h"
#include "MultiView/SceneBootstrapper.h"

namespace oxygen {
class AsyncEngine;
class Graphics;
} // namespace oxygen

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
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = DemoModuleBase;

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  explicit MainModule(const DemoAppContext& app) noexcept;
  ~MainModule() override = default;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MultiViewExample";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::ModulePriority { 500 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override;

  auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool override;
  auto OnShutdown() noexcept -> void override;

  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto ClearBackbufferReferences() -> void override;

protected:
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

private:
  auto AcquireCommandRecorder(Graphics& gfx)
    -> std::shared_ptr<graphics::CommandRecorder>;
  auto TrackSwapchainFramebuffer(graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> void;
  auto MarkSurfacePresentable(engine::FrameContext& context,
    const std::shared_ptr<graphics::Surface>& surface) -> void;
  auto ReleaseAllViews(std::string_view reason) -> void;

  const DemoAppContext& app_;
  SceneBootstrapper scene_bootstrapper_;

  // DemoShell integration (all panels disabled for this non-interactive demo)
  std::unique_ptr<DemoShell> shell_;

  std::vector<std::unique_ptr<DemoView>> views_;
  CompositorGraph compositor_graph_ {};
  ActiveScene active_scene_ {};
  bool initialized_ { false };
};

} // namespace oxygen::examples::multiview
