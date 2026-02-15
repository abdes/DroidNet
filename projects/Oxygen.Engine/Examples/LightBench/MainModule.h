//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "LightBench/LightBenchPanel.h"
#include "LightBench/LightScene.h"

namespace oxygen::renderer {
struct CompositionView;
} // namespace oxygen::renderer

namespace oxygen::examples::light_bench {

//! Main module for the LightBench demo.
/*!
 Provides a minimal DemoShell-driven reference scene for validating
 physically based lighting and exposure workflows.

 @see DemoShell
*/
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::DemoModuleBase;

  explicit MainModule(const oxygen::examples::DemoAppContext& app);

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
      PhaseId::kGameplay, PhaseId::kPublishViews, PhaseId::kGuiUpdate,
      PhaseId::kPreRender, PhaseId::kCompositing, PhaseId::kFrameEnd>();
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttachedImpl(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(observer_ptr<oxygen::engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<oxygen::engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  auto ClearBackbufferReferences() -> void override;

  auto UpdateComposition(engine::FrameContext& context,
    std::vector<renderer::CompositionView>& views) -> void override;

private:
  ActiveScene active_scene_ {};
  scene::NodeHandle registered_view_camera_ {};
  scene::SceneNode main_camera_ {};

  LightScene light_scene_ {};

  std::shared_ptr<LightBenchPanel> light_bench_panel_ {};

  // Hosted view
  ViewId main_view_id_ { kInvalidViewId };
};

} // namespace oxygen::examples::light_bench
