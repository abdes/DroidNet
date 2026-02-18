//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scripting/api_export.h>

struct lua_State;

namespace oxygen::scripting {

struct ScriptExecutionResult {
  bool ok { false };
  std::string stage;
  std::string message;
};

using ScriptSourceText
  = NamedType<std::string_view, struct ScriptSourceTextTag>;
using ScriptChunkName = NamedType<std::string_view, struct ScriptChunkNameTag>;

struct ScriptExecutionRequest {
  ScriptSourceText source_text;
  ScriptChunkName chunk_name { std::string_view { "runtime" } };
};

/*!
 Engine scripting module facade.

 Consumers should discover this module via the engine `ModuleManager` typed
 lookup/subscription pattern (through `AsyncEngine::GetModule<T>()` and module
 attach/detach notifications). Runtime script compilation is owned by
 `AsyncEngine`; this module only registers/unregisters language compilers.
*/
class ScriptingModule final : public engine::EngineModule {
  OXYGEN_TYPED(ScriptingModule)

public:
  OXGN_SCRP_API explicit ScriptingModule(engine::ModulePriority priority);
  OXGN_SCRP_API ~ScriptingModule() override;

  OXYGEN_MAKE_NON_COPYABLE(ScriptingModule)
  OXYGEN_MAKE_NON_MOVABLE(ScriptingModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "ScriptingModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return priority_;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kFixedSimulation, core::PhaseId::kGameplay,
      core::PhaseId::kSceneMutation, core::PhaseId::kFrameEnd>();
  }

  OXGN_SCRP_NDAPI auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;
  OXGN_SCRP_API auto OnShutdown() noexcept -> void override;

  OXGN_SCRP_API auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  OXGN_SCRP_API auto OnFixedSimulation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;
  OXGN_SCRP_API auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_SCRP_API auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_SCRP_API auto OnFrameEnd(observer_ptr<engine::FrameContext> context)
    -> void override;

  OXGN_SCRP_NDAPI auto ExecuteScript(const ScriptExecutionRequest& request)
    -> ScriptExecutionResult;
  OXGN_SCRP_NDAPI auto ExecuteScript(const ScriptSourceBlob& blob)
    -> ScriptExecutionResult;

private:
  auto InitializeSandbox() -> ScriptExecutionResult;
  auto InvokePhaseHook(std::string_view hook_name,
    observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult;
  auto ReportHookError(observer_ptr<engine::FrameContext> context,
    std::string_view hook_name, const ScriptExecutionResult& result) const
    -> void;

  lua_State* lua_state_ { nullptr };
  int runtime_env_ref_;
  engine::ModulePriority priority_;
  observer_ptr<AsyncEngine> engine_ {};
};

} // namespace oxygen::scripting
