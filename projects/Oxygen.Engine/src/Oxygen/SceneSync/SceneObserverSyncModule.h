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
#include <Oxygen/SceneSync/api_export.h>

namespace oxygen::scenesync {

class SceneObserverSyncModule final : public engine::EngineModule {
  OXYGEN_TYPED(SceneObserverSyncModule)

public:
  OXGN_SCNSYNC_API explicit SceneObserverSyncModule(
    engine::ModulePriority priority);
  OXGN_SCNSYNC_API ~SceneObserverSyncModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(SceneObserverSyncModule)
  OXYGEN_MAKE_NON_MOVABLE(SceneObserverSyncModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "SceneObserverSyncModule";
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

  OXGN_SCNSYNC_API auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_SCNSYNC_API auto OnSceneMutation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;

private:
  static auto SyncObservers(observer_ptr<engine::FrameContext> context) -> void;

  engine::ModulePriority priority_;
};

} // namespace oxygen::scenesync
