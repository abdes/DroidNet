//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
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
  OXGN_SCRP_NDAPI auto RegisterBindingPack(
    bindings::contracts::ScriptBindingPackPtr pack) -> bool;
  OXGN_SCRP_NDAPI auto UnregisterBindingPack(std::string_view pack_name)
    -> bool;

private:
  struct SlotRuntimeKey {
    scene::NodeHandle node_handle {};
    uint32_t slot_index { 0 };

    [[nodiscard]] auto operator==(const SlotRuntimeKey&) const noexcept -> bool
      = default;
  };

  struct SlotRuntimeKeyHash {
    [[nodiscard]] auto operator()(const SlotRuntimeKey& key) const noexcept
      -> std::size_t
    {
      std::size_t seed = std::hash<scene::NodeHandle> {}(key.node_handle);
      seed ^= std::hash<uint32_t> {}(key.slot_index) + 0x9e3779b9 + (seed << 6U)
        + (seed >> 2U);
      return seed;
    }
  };

  struct SlotRuntimeState {
    std::shared_ptr<const ScriptExecutable> executable;
    int module_ref { -1 };
    int tick_ref { -1 };
    bool failed_initialization { false };
    bool reported_initialization_error { false };
    std::string initialization_error;
  };

  auto InitializeSandbox() -> ScriptExecutionResult;
  auto InvokePhaseHook(std::string_view hook_name,
    observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult;
  auto RunSceneScripts(observer_ptr<engine::FrameContext> context)
    -> ScriptExecutionResult;
  auto ExecuteSlotTick(const SlotRuntimeKey& key,
    SlotRuntimeState& runtime_state, const scene::SceneNode& node,
    const scene::ScriptingComponent::Slot& slot,
    observer_ptr<engine::FrameContext> context, float dt_seconds)
    -> ScriptExecutionResult;
  auto RebuildSlotRuntime(const SlotRuntimeKey& key, SlotRuntimeState& state,
    const scene::ScriptingComponent::Slot& slot) -> ScriptExecutionResult;
  auto DestroySlotRuntime(SlotRuntimeState& state) -> void;
  auto CleanupStaleSlotRuntimes(
    const std::unordered_set<SlotRuntimeKey, SlotRuntimeKeyHash>& active_keys)
    -> void;
  auto RegisterDefaultBindingPacks() -> bool;
  auto ReportHookError(observer_ptr<engine::FrameContext> context,
    std::string_view hook_name, const ScriptExecutionResult& result) const
    -> void;

  lua_State* lua_state_ { nullptr };
  int runtime_env_ref_;
  engine::ModulePriority priority_;
  observer_ptr<AsyncEngine> engine_ {};
  std::vector<bindings::contracts::ScriptBindingPackPtr> binding_packs_ {};
  std::unordered_map<SlotRuntimeKey, SlotRuntimeState, SlotRuntimeKeyHash>
    slot_runtimes_;
};

} // namespace oxygen::scripting
