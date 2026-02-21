//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/PhysicsModule/api_export.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics {

class PhysicsModule final : public engine::EngineModule,
                            public scene::ISceneObserver {
  OXYGEN_TYPED(PhysicsModule)

public:
  //! Priority contract: must run after gameplay mutators (including
  //! ScriptingModule) and before RendererModule.
  OXGN_PHSYNC_API explicit PhysicsModule(engine::ModulePriority priority);
  OXGN_PHSYNC_API ~PhysicsModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PhysicsModule)
  OXYGEN_MAKE_NON_MOVABLE(PhysicsModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "PhysicsModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return priority_;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kGameplay,
      core::PhaseId::kSceneMutation>();
  }

  OXGN_PHSYNC_API auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;
  OXGN_PHSYNC_API auto OnShutdown() noexcept -> void override;

  OXGN_PHSYNC_API auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_PHSYNC_API auto OnSceneMutation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;

  // ISceneObserver implementation
  auto OnTransformChanged(const scene::NodeHandle& node_handle) noexcept
    -> void override;

private:
  engine::ModulePriority priority_;
  observer_ptr<AsyncEngine> engine_;
};

} // namespace oxygen::physics
