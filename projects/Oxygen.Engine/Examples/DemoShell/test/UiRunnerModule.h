//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoModuleBase.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>

struct ImGuiTestEngine;

namespace oxygen::examples::testing {

//! The application module of the DemoShell UI test runner.
/*!
 Hosts the DemoShell on an empty scene with one camera, so the shell's own
 widgets and panels are tested without any example's content. It registers
 only the DemoShell tests; the examples register their own.

 @see DemoModuleBase, UiTestSession
*/
class UiRunnerModule final : public DemoModuleBase {
  OXYGEN_TYPED(UiRunnerModule)
public:
  using Base = DemoModuleBase;

  explicit UiRunnerModule(const host::HostContext& host);
  ~UiRunnerModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(UiRunnerModule)
  OXYGEN_MAKE_NON_MOVABLE(UiRunnerModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "DemoShellUiRunner";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr engine::ModulePriority kPriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return kDemoPhases;
  }

  auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  auto RegisterUiTests(ImGuiTestEngine* engine) -> void override;
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  auto OnScenePublished(engine::FrameContext& context) -> void override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;

private:
  //! Stages an empty scene with a camera.
  auto StageScene() -> void;

  host::ActiveScene active_scene_;
  ViewId main_view_id_ { kInvalidViewId };
  scene::SceneNode main_camera_ {};
};

} // namespace oxygen::examples::testing
