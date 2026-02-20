//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Detail/NamedType_skills.h>
#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>
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

using ScriptingSessionId
  = NamedType<uint64_t, struct ScriptingSessionIdTag, Comparable>;
constexpr ScriptingSessionId kInvalidScriptingSessionId { 0 };

using Task = std::function<void(lua_State*)>;

class ScriptTaskQueue {
public:
  ScriptTaskQueue() = default;

  ~ScriptTaskQueue() { DrainAndDestroyPendingTasks(); }

  OXYGEN_MAKE_NON_COPYABLE(ScriptTaskQueue)
  OXYGEN_MAKE_NON_MOVABLE(ScriptTaskQueue)

  auto StartSession() -> void
  {
    current_session_id_.fetch_add(1, std::memory_order_acq_rel);
    DrainAndDestroyPendingTasks();
  }

  auto EndSession() -> void { DrainAndDestroyPendingTasks(); }

  auto Submit(Task task, ScriptingSessionId session_id) -> void
  {
    if (!task) {
      return;
    }

    const auto active_session_id
      = current_session_id_.load(std::memory_order_acquire);
    if (session_id.get() != active_session_id) {
      return;
    }

    auto* node = new PendingTaskNode {
      .task = std::move(task),
      .session_id = session_id.get(),
      .next = nullptr,
    };

    auto* head = pending_head_.load(std::memory_order_relaxed);
    do {
      node->next = head;
    } while (!pending_head_.compare_exchange_weak(
      head, node, std::memory_order_release, std::memory_order_relaxed));
  }

  auto Process(lua_State* state) -> void
  {
    auto* pending = pending_head_.exchange(nullptr, std::memory_order_acq_rel);
    pending = ReverseList(pending);

    const auto active_session_id
      = current_session_id_.load(std::memory_order_acquire);

    while (pending != nullptr) {
      auto* node = pending;
      pending = pending->next;

      if (node->task != nullptr && node->session_id == active_session_id) {
        node->task(state);
      }

      delete node;
    }
  }

  [[nodiscard]] auto GetSessionId() const -> ScriptingSessionId
  {
    return ScriptingSessionId { current_session_id_.load(
      std::memory_order_acquire) };
  }

private:
  struct PendingTaskNode final {
    Task task;
    uint64_t session_id { 0 };
    PendingTaskNode* next { nullptr };
  };

  static auto ReverseList(PendingTaskNode* head) -> PendingTaskNode*
  {
    PendingTaskNode* prev = nullptr;
    auto* current = head;
    while (current != nullptr) {
      auto* next = current->next;
      current->next = prev;
      prev = current;
      current = next;
    }
    return prev;
  }

  auto DrainAndDestroyPendingTasks() -> void
  {
    auto* pending = pending_head_.exchange(nullptr, std::memory_order_acq_rel);
    while (pending != nullptr) {
      auto* node = pending;
      pending = pending->next;
      delete node;
    }
  }

  std::atomic<uint64_t> current_session_id_ {
    kInvalidScriptingSessionId.get()
  };
  std::atomic<PendingTaskNode*> pending_head_ { nullptr };
};

/*!
  Primary engine module for Lua/Luau script execution and lifetime management.

  The ScriptingModule manages the global Lua state, initializes the sandboxed
  environment, and executes scripts attached to SceneNodes via the
  ScriptingComponent.

  ### Key Responsibilities
  - **Environment Sandboxing**: Ensures scripts run in isolated environments
    with restricted access to dangerous Lua globals.
  - **Hot-Reload Integration**: Subscribes to AssetLoader events to
    automatically update running script instances when disk files change.
  - **Lifecycle Management**: Invokes Lua hooks (on_gameplay, on_scene_mutation)
    during specific engine phases.
  - **Binding Pack Registration**: Serves as the registry for C++ engine
    bindings exposed to Lua.

  ### Design Contracts
  - **Thread-Safety**: All Lua execution occurs on the main engine thread.
    Cross-thread tasks must be submitted via `SubmitMainThreadTask`.
  - **Instance Isolation**: Each script instance (Slot) has its own private
    global table, preventing side-effects between different game objects.
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

  OXGN_SCRP_NDAPI auto ExecuteScript(const ScriptExecutionRequest& request)
    -> ScriptExecutionResult;
  OXGN_SCRP_NDAPI auto ExecuteScript(const ScriptSourceBlob& blob)
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
    uint32_t slot_index { 0 };

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
  };

  struct SlotRuntimeState {
    std::shared_ptr<const ScriptExecutable> executable;
    data::AssetKey asset_key;
    uint64_t last_known_hash { 0 };
    int module_ref { -1 };
    int on_gameplay_ref { -1 };
    int on_scene_mutation_ref { -1 };
    int instance_env_ref { -1 };
    bool failed_initialization { false };
    bool reported_initialization_error { false };
    std::string initialization_error;
  };

  auto InitializeSandbox() -> ScriptExecutionResult;
  auto InitializeInstanceEnvironment(SlotRuntimeState& state) -> void;
  auto InvokePhaseHook(std::string_view hook_name,
    observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult;
  auto RunSceneScripts(observer_ptr<engine::FrameContext> context)
    -> ScriptExecutionResult;
  auto RunSceneMutationScripts(observer_ptr<engine::FrameContext> context)
    -> ScriptExecutionResult;
  auto ExecuteSlotGameplay(const SlotRuntimeKey& key,
    SlotRuntimeState& runtime_state, const scene::SceneNode& node,
    const scene::ScriptingComponent::Slot& slot,
    observer_ptr<engine::FrameContext> context, float dt_seconds)
    -> ScriptExecutionResult;
  auto ExecuteSlotSceneMutation(const SlotRuntimeKey& key,
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
  auto ProcessPendingTasks() -> void;
  auto RegisterDefaultBindingPacks() -> bool;
  auto ReportHookError(observer_ptr<engine::FrameContext> context,
    std::string_view hook_name, const ScriptExecutionResult& result) const
    -> void;
  auto CollectActiveScripts(observer_ptr<engine::FrameContext> context) -> void;

  lua_State* lua_state_ { nullptr };
  int runtime_env_ref_ { -1 };
  int global_env_ref_ { -1 };
  engine::ModulePriority priority_;
  observer_ptr<AsyncEngine> engine_;
  bool input_bridge_logs_enabled_ { false };
  int input_bridge_log_verbosity_ { 2 };
  std::vector<bindings::contracts::ScriptBindingPackPtr> binding_packs_;
  input::InputScriptEventBridge input_event_bridge_ {};
  content::IAssetLoader::EvictionSubscription script_reload_subscription_;
  std::unordered_map<SlotRuntimeKey, SlotRuntimeState, SlotRuntimeKeyHash>
    slot_runtimes_;
  std::vector<ActiveScriptSlot> active_frame_slots_;

  std::mutex pending_tasks_mutex_;
  std::deque<Task>
    pending_tasks_; // Deprecated by ScriptTaskQueue but kept for transition if
                    // needed, though we will replace usage.
  ScriptTaskQueue task_queue_;
};

} // namespace oxygen::scripting
