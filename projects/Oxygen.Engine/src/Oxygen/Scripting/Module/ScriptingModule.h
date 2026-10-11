//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Detail/NamedType_skills.h>
#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>
#include <Oxygen/Scripting/api_export.h>

struct lua_State;

namespace oxygen {
class IAsyncEngine;
}

namespace oxygen::scripting {

namespace bindings {
  struct EventDispatchStatus;
}

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

using ScriptingSessionId
  = NamedType<uint64_t, struct ScriptingSessionIdTag, Comparable>;
constexpr ScriptingSessionId kInvalidScriptingSessionId { 0 };

using Task = std::function<void(lua_State*)>;

//! Main-thread task queue fed from any thread.
/*!
 Tasks submitted for a session that is no longer current are dropped, both at
 submission and when processed.
*/
class ScriptTaskQueue {
public:
  ScriptTaskQueue() = default;
  ~ScriptTaskQueue() = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptTaskQueue)
  OXYGEN_MAKE_NON_MOVABLE(ScriptTaskQueue)

  auto StartSession() -> void
  {
    current_session_id_.fetch_add(1, std::memory_order_acq_rel);
    DrainPendingTasks();
  }

  auto EndSession() -> void { DrainPendingTasks(); }

  auto Submit(Task task, ScriptingSessionId session_id) -> void
  {
    if (!task || session_id != GetSessionId()) {
      return;
    }
    const std::scoped_lock lock(mutex_);
    pending_.push_back(PendingTask {
      .task = std::move(task),
      .session_id = session_id,
    });
  }

  auto Process(lua_State* state) -> void
  {
    std::vector<PendingTask> pending;
    {
      const std::scoped_lock lock(mutex_);
      pending.swap(pending_);
    }
    const auto active_session_id = GetSessionId();
    for (auto& entry : pending) {
      if (entry.session_id == active_session_id) {
        entry.task(state);
      }
    }
  }

  [[nodiscard]] auto GetSessionId() const -> ScriptingSessionId
  {
    return ScriptingSessionId { current_session_id_.load(
      std::memory_order_acquire) };
  }

private:
  struct PendingTask {
    Task task;
    ScriptingSessionId session_id;
  };

  auto DrainPendingTasks() -> void
  {
    std::vector<PendingTask> pending;
    const std::scoped_lock lock(mutex_);
    pending.swap(pending_);
  }

  std::atomic<uint64_t> current_session_id_ {
    kInvalidScriptingSessionId.get()
  };
  std::mutex mutex_;
  std::vector<PendingTask> pending_;
};

/*!
  Primary engine module for Lua/Luau script execution and lifetime management.

  The ScriptingModule manages the global Lua state, initializes the sandboxed
  environment, and executes scripts attached to SceneNodes via the
  ScriptingComponent.

  Each ready slot is one script instance. Its chunk returns the table made by
  `oxygen.script`; the instance `self` indexes that table and carries
  `self.node` and the read-only `self.params`. Hooks are optional methods:
  `on_start(self)` runs once in the first Gameplay phase after the slot is
  ready, before any `on_gameplay` of that phase; `on_fixed_simulation`,
  `on_gameplay` and `on_scene_mutation` receive `(self, dt)`; and
  `on_destroy(self)` runs when a started instance ends (slot removed, node
  destroyed, scene changed, executable changed or module shut down). Within a
  phase, instances run by execution order, then node order, then slot
  position. Event and input listeners registered by an instance are released
  after its `on_destroy`.

  ### Key Responsibilities
  - **Environment Sandboxing**: Ensures scripts run in isolated environments
    with restricted access to dangerous Lua globals.
  - **Hot-Reload Integration**: Subscribes to AssetLoader events to
    automatically update running script instances when disk files change.
  - **Lifecycle Management**: Starts, runs and ends script instances during
    the engine phases.
  - **Binding Pack Registration**: Serves as the registry for C++ engine
    bindings exposed to Lua.

  ### Design Contracts
  - **Thread-Safety**: All Lua execution occurs on the
  main engine thread.
    Cross-thread tasks must be submitted via
  `SubmitMainThreadTask`.
  - **Instance Isolation**: Each script instance
  (Slot) has its own private
    global table, preventing side-effects between
  different game objects.
    Slot initialization and execution failures are content errors. A faulted
    slot stops executing until its executable changes; healthy slots and the
    compiler remain available.
  - **Priority Contract**: Must run before
  `SceneObserverSyncModule` so
    gameplay registration is complete before
  deferred scene mutations are
    dispatched. Use
  `engine::kScriptingModulePriority` (near highest).
*/
class ScriptingModule final : public engine::EngineModule,
                              public scene::ISceneObserver {
  OXYGEN_TYPED(ScriptingModule)

public:
  OXGN_SCRP_API explicit ScriptingModule(engine::ModulePriority priority);
  OXGN_SCRP_API ~ScriptingModule() override;

  OXYGEN_MAKE_NON_COPYABLE(ScriptingModule)
  OXYGEN_DEFAULT_MOVABLE(ScriptingModule)

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

  OXGN_SCRP_NDAPI auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool override;
  OXGN_SCRP_API auto OnShutdown() noexcept -> void override;
  OXGN_SCRP_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_SCRP_API auto ApplyConsoleCVars(
    observer_ptr<const console::Console> console) noexcept -> void override;

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

  OXGN_SCRP_API auto OnScriptSlotActivated(const scene::NodeHandle& node_handle,
    scene::ScriptSlotIndex slot_index,
    const scene::ScriptingComponent::Slot& slot) noexcept -> void override;
  OXGN_SCRP_API auto OnScriptSlotChanged(const scene::NodeHandle& node_handle,
    scene::ScriptSlotIndex slot_index,
    const scene::ScriptingComponent::Slot& slot) noexcept -> void override;
  OXGN_SCRP_API auto OnScriptSlotDeactivated(
    const scene::NodeHandle& node_handle,
    scene::ScriptSlotIndex slot_index) noexcept -> void override;

  //! For testing only. Should never be used for engine runtime.
  OXGN_SCRP_NDAPI auto ExecuteScript(const ScriptExecutionRequest& request)
    -> ScriptExecutionResult;
  OXGN_SCRP_NDAPI auto RegisterBindingPack(
    bindings::contracts::ScriptBindingPackPtr pack) -> bool;
  OXGN_SCRP_NDAPI auto UnregisterBindingPack(std::string_view pack_name)
    -> bool;

  //! Submits a task to be executed on the main thread during the next frame
  //! start. This method is thread-safe and can be called from any thread.
  //! The task will only be executed if the session_id matches the current
  //! session.
  OXGN_SCRP_API auto SubmitMainThreadTask(
    Task task, ScriptingSessionId session_id) -> void;

  [[nodiscard]] OXGN_SCRP_API auto GetSessionId() const -> ScriptingSessionId;

private:
  struct SlotRuntimeKey {
    scene::NodeHandle node_handle;
    scene::ScriptSlotIndex slot_index;

    [[nodiscard]] auto operator==(const SlotRuntimeKey&) const noexcept -> bool
      = default;
  };

  struct SlotRuntimeKeyHash {
    [[nodiscard]] auto operator()(const SlotRuntimeKey& key) const noexcept
      -> std::size_t
    {
      std::size_t seed = std::hash<scene::NodeHandle> {}(key.node_handle);
      HashCombine(seed, key.slot_index);
      return seed;
    }
  };

  struct ActiveScriptSlot {
    SlotRuntimeKey key;
    int32_t execution_order { 0 };
  };

  //! One script instance: the slot's script table and its `self`.
  struct SlotRuntimeState {
    std::shared_ptr<const ScriptExecutable> executable;
    data::AssetKey asset_key;
    uint64_t last_known_fingerprint { 0 };
    int instance_env_ref { -1 };
    int script_ref { -1 };
    int self_ref { -1 };
    //! `on_start` completed; only started instances receive other hooks.
    bool started { false };
    //! Loading or a hook failed; no further hooks until the executable
    //! changes.
    bool faulted { false };
    uint64_t event_owner_id { 0 };
    Uuid component_incarnation;
    std::optional<scene::ScriptingComponent::Slot> slot_identity;
  };

  //! Per-frame instance hooks, each run in instance order.
  enum class InstanceHook : uint8_t {
    kFixedSimulation,
    kGameplay,
    kSceneMutation,
  };

  auto InitializeSandbox() -> ScriptExecutionResult;
  auto InvokePhaseHook(std::string_view hook_name,
    observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult;
  auto RunInstances(
    observer_ptr<engine::FrameContext> context, InstanceHook hook) -> void;
  auto PrepareInstance(const SlotRuntimeKey& key,
    observer_ptr<engine::FrameContext> context) -> SlotRuntimeState*;
  auto CreateInstance(const SlotRuntimeKey& key, SlotRuntimeState& state,
    scene::SceneNode& node, const scene::ScriptingComponent::Slot& slot)
    -> ScriptExecutionResult;
  auto PushInstanceParams(const scene::ScriptingComponent::Slot& slot) -> void;
  auto CallInstanceHook(SlotRuntimeState& state, std::string_view hook_name,
    std::optional<float> dt_seconds) -> ScriptExecutionResult;
  auto FaultInstance(const SlotRuntimeKey& key, SlotRuntimeState& state,
    observer_ptr<engine::FrameContext> context, std::string_view hook_name,
    const ScriptExecutionResult& result) -> void;
  auto EndInstance(SlotRuntimeState& state) -> void;
  [[nodiscard]] auto IsCurrentSlotRuntime(
    const SlotRuntimeKey& key, const SlotRuntimeState& state) const -> bool;
  auto EnsureSceneObservation(observer_ptr<engine::FrameContext> context)
    -> void;
  auto ActivateSlot(const SlotRuntimeKey& key,
    const scene::ScriptingComponent::Slot& slot) -> void;
  auto DeactivateSlot(const SlotRuntimeKey& key) -> void;
  auto UpdateSlot(const SlotRuntimeKey& key,
    const scene::ScriptingComponent::Slot& slot) -> void;
  auto SortActiveSlots() -> void;
  auto EndAllInstances() -> void;
  auto ProcessPendingTasks() -> void;
  auto RegisterDefaultBindingPacks() -> bool;
  auto ReportHookError(observer_ptr<engine::FrameContext> context,
    std::string_view hook_name, const ScriptExecutionResult& result) const
    -> void;
  auto ReportEventDispatchErrors(observer_ptr<engine::FrameContext> context,
    std::string_view phase, const bindings::EventDispatchStatus& status)
    -> void;

  lua_State* lua_state_ { nullptr };
  int runtime_env_ref_ { -1 };
  int global_env_ref_ { -1 };
  engine::ModulePriority priority_;
  observer_ptr<IAsyncEngine> engine_;
  bool attached_ { false };
  bool input_bridge_logs_enabled_ { false };
  int input_bridge_log_verbosity_ { 2 };
  std::vector<bindings::contracts::ScriptBindingPackPtr> binding_packs_;
  input::InputScriptEventBridge input_event_bridge_ {};
  content::IAssetLoader::EvictionSubscription script_reload_subscription_;
  std::unordered_map<SlotRuntimeKey, SlotRuntimeState, SlotRuntimeKeyHash>
    slot_runtimes_;
  //! Active slots in instance order once sorted: execution order, then node
  //! order, then slot position.
  std::vector<ActiveScriptSlot> active_frame_slots_;
  std::unordered_map<SlotRuntimeKey, size_t, SlotRuntimeKeyHash>
    active_slot_indices_;
  bool active_slots_sorted_ { true };
  std::weak_ptr<scene::Scene> observed_scene_;

  ScriptTaskQueue task_queue_;
};

} // namespace oxygen::scripting
