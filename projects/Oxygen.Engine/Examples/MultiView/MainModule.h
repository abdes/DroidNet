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
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/Scene.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/ExampleModuleBase.h"
#include "DemoView.h"
#include "SceneBootstrapper.h"

namespace oxygen {
class Graphics;
}

namespace oxygen::examples::multiview {

//! Multi-view rendering example demonstrating Phase 2 features.
/*!
 This example showcases multi-view rendering with:
 - Main view: Full-screen solid-shaded sphere
 - PiP view: Top-right corner (25% size)

 Both views render the same scene with different cameras.
 Demonstrates PrepareView/RenderView APIs and per-view state isolation.
*/
class MainModule final : public common::ExampleModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = ExampleModuleBase;

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  explicit MainModule(const common::AsyncEngineApp& app) noexcept;
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

  const common::AsyncEngineApp& app_;
  SceneBootstrapper scene_bootstrapper_;

  std::vector<std::unique_ptr<DemoView>> views_;
  bool initialized_ { false };
};

} // namespace oxygen::examples::multiview
