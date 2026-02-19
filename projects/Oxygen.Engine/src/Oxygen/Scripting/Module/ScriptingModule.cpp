//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/Traversal.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/CoreBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Input/InputBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindingPack.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

namespace oxygen::scripting {

namespace {
  constexpr int kLuaNoArgs = 0;
  constexpr int kLuaNoResults = 0;
  constexpr int kLuaStackTop = -1;
  constexpr int kLuaStackSecondTop = -2;
  constexpr int kLuaSingleValueCount = 1;
  constexpr int kLuaErrorAndTracebackCount = 2;
  constexpr int kLuaEnvironmentAndErrorCount = 2;
  constexpr int kLuaTracebackIndex = 1;
  constexpr const char* kLuaTracebackFnName = "LuaTraceback";
  constexpr int kLuaNoRef = -1;
  constexpr std::string_view kCVarScriptingInputBridgeLogs
    = "scrp.input_bridge_logs";
  constexpr std::string_view kCVarScriptingInputBridgeLogVerbosity
    = "scrp.input_bridge_log_verbosity";
  constexpr int64_t kMinInputBridgeLogVerbosity = 0;
  constexpr int64_t kMaxInputBridgeLogVerbosity = 9;

  class ScopedActiveFrameContext final {
  public:
    ScopedActiveFrameContext(lua_State* state,
      const observer_ptr<engine::FrameContext> frame_context) noexcept
      : state_(state)
      , previous_(bindings::GetActiveFrameContext(state))
    {
      bindings::SetActiveFrameContext(state_, frame_context);
    }

    ~ScopedActiveFrameContext()
    {
      if (state_ != nullptr) {
        bindings::SetActiveFrameContext(state_, previous_);
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(ScopedActiveFrameContext)
    OXYGEN_DEFAULT_MOVABLE(ScopedActiveFrameContext)

  private:
    lua_State* state_ { nullptr };
    observer_ptr<engine::FrameContext> previous_;
  };

  auto LuaTraceback(lua_State* state) -> int
  {
    constexpr int kMessageIndex = 1;
    const auto* message = lua_tostring(state, kMessageIndex);
    luaL_traceback(state, state, message, kMessageIndex);
    return kMessageIndex;
  }

  auto LuaToString(lua_State* state, int index) -> std::string
  {
    if (const auto* text = lua_tostring(state, index); text != nullptr) {
      return text;
    }
    return "unknown lua error";
  }

  auto OkResult() -> ScriptExecutionResult
  {
    return ScriptExecutionResult {
      .ok = true,
      .stage = "ok",
      .message = {},
    };
  }

  auto ErrorResult(std::string stage, std::string message)
    -> ScriptExecutionResult
  {
    return ScriptExecutionResult {
      .ok = false,
      .stage = std::move(stage),
      .message = std::move(message),
    };
  }

  auto IsValidLuaRef(const int ref) noexcept -> bool { return ref >= 0; }

  auto TryGetHookFunction(lua_State* state, std::string_view hook_name) -> bool
  {
    // Preferred: oxygen.<hook_name>()
    lua_getglobal(state, "oxygen");
    if (lua_istable(state, kLuaStackTop)) {
      lua_getfield(state, kLuaStackTop, std::string(hook_name).c_str());
      if (lua_isfunction(state, kLuaStackTop)) {
        lua_remove(
          state, kLuaStackSecondTop); // remove oxygen table, keep function
        return true;
      }
      lua_pop(state, kLuaSingleValueCount); // pop non-function field
    }
    lua_pop(
      state, kLuaSingleValueCount); // pop oxygen table or non-table global

    // Fallback: global hook function.
    lua_getglobal(state, std::string(hook_name).c_str());
    if (lua_isfunction(state, kLuaStackTop)) {
      return true;
    }

    lua_pop(state, kLuaSingleValueCount);
    return false;
  }

  auto CreateRuntimeEnvironment(lua_State* state) -> int
  {
    lua_newtable(state); // env

    constexpr std::array<const char*, 21> kSafeGlobals = {
      "_VERSION",
      "assert",
      "error",
      "ipairs",
      "next",
      "pairs",
      "pcall",
      "print",
      "select",
      "tonumber",
      "tostring",
      "type",
      "xpcall",
      "coroutine",
      "math",
      "string",
      "table",
      "utf8",
      "bit32",
      "vector",
      "buffer",
    };

    for (const auto* const global_name : kSafeGlobals) {
      lua_getglobal(state, global_name);
      if (lua_isnil(state, kLuaStackTop)) {
        lua_pop(state, kLuaSingleValueCount);
        continue;
      }
      lua_setfield(state, kLuaStackSecondTop, global_name);
    }

    lua_pushvalue(state, kLuaStackTop);
    lua_setfield(state, kLuaStackSecondTop, "_G");
    const auto ref = lua_ref(state, kLuaStackTop);
    lua_settop(state, 0);
    return ref;
  }

  auto TryGetHookFunctionFromEnvironment(
    lua_State* state, const int env_ref, std::string_view hook_name) -> bool
  {
    if (env_ref == LUA_NOREF || env_ref == LUA_REFNIL) {
      return false;
    }

    lua_getref(state, env_ref);
    if (!lua_istable(state, kLuaStackTop)) {
      lua_pop(state, kLuaSingleValueCount);
      return false;
    }

    lua_getfield(state, kLuaStackTop, std::string(hook_name).c_str());
    if (lua_isfunction(state, kLuaStackTop)) {
      lua_remove(state, kLuaStackSecondTop); // keep function
      return true;
    }

    lua_pop(state, 2); // value + env table
    return false;
  }

} // namespace

ScriptingModule::ScriptingModule(const engine::ModulePriority priority)
  : runtime_env_ref_(LUA_NOREF)
  , priority_(priority)
{
}

ScriptingModule::~ScriptingModule() { OnShutdown(); }

auto ScriptingModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
  -> bool
{
  engine_ = engine;
  if (engine_ != nullptr) {
    (void)engine_->GetScriptCompilationService().RegisterCompiler(
      std::make_shared<LuauScriptCompiler>());
  }

  lua_state_ = luaL_newstate();
  if (lua_state_ == nullptr) {
    return false;
  }
  bindings::SetActiveEngine(lua_state_, engine_);

  if (binding_packs_.empty()) {
    if (!RegisterDefaultBindingPacks()) {
      OnShutdown();
      return false;
    }
  }

  const auto sandbox_result = InitializeSandbox();
  if (!sandbox_result.ok) {
    DLOG_F(ERROR, "scripting bootstrap failed: {}", sandbox_result.message);
    OnShutdown();
    return false;
  }

  return true;
}

auto ScriptingModule::OnShutdown() noexcept -> void
{
  if (engine_ != nullptr) {
    (void)engine_->GetScriptCompilationService().UnregisterCompiler(
      data::pak::ScriptLanguage::kLuau);
    engine_ = nullptr;
  }

  if (lua_state_ != nullptr) {
    bindings::SetActiveFrameContext(lua_state_, {});
    bindings::SetActiveEngine(lua_state_, {});

    for (auto& [_, runtime] : slot_runtimes_) {
      DestroySlotRuntime(runtime);
    }
    slot_runtimes_.clear();

    bindings::ShutdownEventsRuntime(lua_state_);

    if (runtime_env_ref_ != LUA_NOREF) {
      lua_unref(lua_state_, runtime_env_ref_);
      runtime_env_ref_ = LUA_NOREF;
    }
    lua_close(lua_state_);
    lua_state_ = nullptr;
  }

  binding_packs_.clear();
}

auto ScriptingModule::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarScriptingInputBridgeLogs),
    .help = "Enable/disable ScriptingModule input->events bridge logs",
    .default_value = input_bridge_logs_enabled_,
    .flags = console::CVarFlags::kDevOnly,
    .min_value = {},
    .max_value = {},
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarScriptingInputBridgeLogVerbosity),
    .help = "ScriptingModule input->events bridge log verbosity [0..9]",
    .default_value = static_cast<int64_t>(input_bridge_log_verbosity_),
    .flags = console::CVarFlags::kDevOnly,
    .min_value = static_cast<double>(kMinInputBridgeLogVerbosity),
    .max_value = static_cast<double>(kMaxInputBridgeLogVerbosity),
  });
}

auto ScriptingModule::ApplyConsoleCVars(
  const observer_ptr<const console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  bool logs_enabled = input_bridge_logs_enabled_;
  if (console->TryGetCVarValue<bool>(
        kCVarScriptingInputBridgeLogs, logs_enabled)) {
    input_bridge_logs_enabled_ = logs_enabled;
  }

  int64_t log_verbosity = input_bridge_log_verbosity_;
  if (console->TryGetCVarValue<int64_t>(
        kCVarScriptingInputBridgeLogVerbosity, log_verbosity)) {
    input_bridge_log_verbosity_ = static_cast<int>(log_verbosity);
  }
}

auto ScriptingModule::RegisterDefaultBindingPacks() -> bool
{
  auto default_packs = std::array {
    bindings::CreateCoreBindingPack(),
    bindings::CreateContentBindingPack(),
    bindings::CreateInputBindingPack(),
    bindings::CreateSceneBindingPack(),
  };

  for (auto& pack : default_packs) {
    if (!pack || !RegisterBindingPack(std::move(pack))) {
      DLOG_F(ERROR, "failed to register default scripting pack");
      return false;
    }
  }

  return true;
}

auto ScriptingModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  if (lua_state_ != nullptr) {
    bindings::SetActiveEventPhase(lua_state_, "frame_start");
    bindings::QueueEngineEvent(lua_state_, "frame.start", "frame_start");
  }

  const auto result = InvokePhaseHook("on_frame_start", context);
  if (!result.ok) {
    ReportHookError(context, "on_frame_start", result);
  }

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "frame_start");
    if (!dispatch_result.ok && context != nullptr) {
      const auto msg
        = std::string("oxygen.events dispatch failed [frame_start]: ")
            .append(dispatch_result.message);
      DLOG_F(ERROR, "{}", msg);
      ReportError(context, msg);
    }
  }
}

auto ScriptingModule::OnFixedSimulation(
  observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (lua_state_ != nullptr) {
    bindings::SetActiveEventPhase(lua_state_, "fixed_simulation");
  }

  const auto result = InvokePhaseHook("on_fixed_simulation", context);
  if (!result.ok) {
    ReportHookError(context, "on_fixed_simulation", result);
  }

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "fixed_simulation");
    if (!dispatch_result.ok && context != nullptr) {
      const auto msg
        = std::string("oxygen.events dispatch failed [fixed_simulation]: ")
            .append(dispatch_result.message);
      DLOG_F(ERROR, "{}", msg);
      ReportError(context, msg);
    }
  }
  co_return;
}

auto ScriptingModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (lua_state_ != nullptr) {
    bindings::SetActiveEventPhase(lua_state_, "gameplay");
    input_event_bridge_.QueueActionEdgeEvents(lua_state_, context,
      input_bridge_logs_enabled_, input_bridge_log_verbosity_);
  }

  const auto result = InvokePhaseHook("on_gameplay", context);
  if (!result.ok) {
    ReportHookError(context, "on_gameplay", result);
  }

  const auto scene_result = RunSceneScripts(context);
  if (!scene_result.ok) {
    ReportHookError(context, "scene_tick", scene_result);
  }

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "gameplay");
    if (!dispatch_result.ok && context != nullptr) {
      const auto msg = std::string("oxygen.events dispatch failed [gameplay]: ")
                         .append(dispatch_result.message);
      DLOG_F(ERROR, "{}", msg);
      ReportError(context, msg);
    }
  }

  co_return;
}

auto ScriptingModule::OnSceneMutation(
  observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (lua_state_ != nullptr) {
    bindings::SetActiveEventPhase(lua_state_, "scene_mutation");
  }

  const auto result = InvokePhaseHook("on_scene_mutation", context);
  if (!result.ok) {
    ReportHookError(context, "on_scene_mutation", result);
  }

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "scene_mutation");
    if (!dispatch_result.ok && context != nullptr) {
      const auto msg
        = std::string("oxygen.events dispatch failed [scene_mutation]: ")
            .append(dispatch_result.message);
      DLOG_F(ERROR, "{}", msg);
      ReportError(context, msg);
    }
  }
  co_return;
}

auto ScriptingModule::OnFrameEnd(observer_ptr<engine::FrameContext> context)
  -> void
{
  if (lua_state_ != nullptr) {
    bindings::SetActiveEventPhase(lua_state_, "frame_end");
    bindings::QueueEngineEvent(lua_state_, "frame.end", "frame_end");
  }

  const auto result = InvokePhaseHook("on_frame_end", context);
  if (!result.ok) {
    ReportHookError(context, "on_frame_end", result);
  }

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "frame_end");
    if (!dispatch_result.ok && context != nullptr) {
      const auto msg
        = std::string("oxygen.events dispatch failed [frame_end]: ")
            .append(dispatch_result.message);
      DLOG_F(ERROR, "{}", msg);
      ReportError(context, msg);
    }
  }
}

auto ScriptingModule::ExecuteScript(const ScriptExecutionRequest& request)
  -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult(
      "runtime", "cannot execute script: luau module is not attached");
  }

  const Luau::CompileOptions options {
    .optimizationLevel = 1,
    .debugLevel = 2,
  };
  const std::string bytecode
    = Luau::compile(std::string(request.source_text.get()), options);

  lua_getref(lua_state_, runtime_env_ref_);
  const auto env_index = lua_gettop(lua_state_);
  const std::string chunk_name_string { request.chunk_name.get() };
  const auto load_status = luau_load(lua_state_, chunk_name_string.c_str(),
    bytecode.data(), bytecode.size(), env_index);
  if (load_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaEnvironmentAndErrorCount); // error + env
    return ErrorResult("compile_or_load", error_message);
  }
  lua_remove(lua_state_, env_index); // keep chunk only

  lua_pushcfunction(lua_state_, LuaTraceback, kLuaTracebackFnName);
  const auto chunk_index = lua_gettop(lua_state_) - 1;
  lua_insert(lua_state_, chunk_index); // [ ... traceback, chunk ]
  const auto call_status
    = lua_pcall(lua_state_, kLuaNoArgs, kLuaNoResults, chunk_index);
  if (call_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaErrorAndTracebackCount); // error + traceback
    return ErrorResult("runtime", error_message);
  }
  lua_pop(lua_state_, kLuaSingleValueCount); // traceback

  return OkResult();
}

auto ScriptingModule::ExecuteScript(const ScriptSourceBlob& blob)
  -> ScriptExecutionResult
{
  if (blob.IsEmpty()) {
    return ErrorResult("load", "script source blob is empty");
  }

  const auto bytes = blob.BytesView();
  std::string source_text;
  source_text.reserve(bytes.size());
  for (const auto byte : bytes) {
    source_text.push_back(static_cast<char>(byte));
  }
  return ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { source_text },
    .chunk_name = ScriptChunkName { blob.GetCanonicalName().get() },
  });
}

auto ScriptingModule::InitializeSandbox() -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("bootstrap", "lua state is null");
  }

  luaL_openlibs(lua_state_);
  luaL_sandbox(lua_state_);
  runtime_env_ref_ = CreateRuntimeEnvironment(lua_state_);
  for (const auto& pack : binding_packs_) {
    if (!pack
      || !pack->Register(bindings::contracts::ScriptBindingPackContext {
        .lua_state = lua_state_,
        .runtime_env_ref = runtime_env_ref_,
      })) {
      return ErrorResult("bootstrap",
        std::string("failed to register lua engine bindings pack: ")
          .append(pack ? pack->Name() : "<null>"));
    }
  }
  return OkResult();
}

auto ScriptingModule::RegisterBindingPack(
  bindings::contracts::ScriptBindingPackPtr pack) -> bool
{
  if (!pack) {
    return false;
  }

  const auto already_registered
    = std::ranges::any_of(binding_packs_, [&pack](const auto& existing) {
        return existing && existing->Name() == pack->Name();
      });
  if (already_registered) {
    return false;
  }

  const auto* raw_pack = pack.get();
  binding_packs_.push_back(std::move(pack));
  if (lua_state_ != nullptr && runtime_env_ref_ != LUA_NOREF) {
    const bool ok
      = raw_pack->Register(bindings::contracts::ScriptBindingPackContext {
        .lua_state = lua_state_,
        .runtime_env_ref = runtime_env_ref_,
      });
    if (!ok) {
      binding_packs_.erase(
        std::remove_if(binding_packs_.begin(), binding_packs_.end(),
          [raw_pack](const auto& item) { return item.get() == raw_pack; }),
        binding_packs_.end());
      return false;
    }
  }
  return true;
}

auto ScriptingModule::UnregisterBindingPack(std::string_view pack_name) -> bool
{
  const auto old_size = binding_packs_.size();
  binding_packs_.erase(
    std::remove_if(binding_packs_.begin(), binding_packs_.end(),
      [pack_name](
        const auto& pack) { return pack && pack->Name() == pack_name; }),
    binding_packs_.end());
  return old_size != binding_packs_.size();
}

auto ScriptingModule::InvokePhaseHook(const std::string_view hook_name,
  observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("runtime", "lua state is null");
  }

  if (!TryGetHookFunctionFromEnvironment(
        lua_state_, runtime_env_ref_, hook_name)
    && !TryGetHookFunction(lua_state_, hook_name)) {
    return OkResult();
  }
  const ScopedActiveFrameContext active_context(lua_state_, context);

  int arg_count = 0;
  if (context != nullptr) {
    using seconds_f = std::chrono::duration<float>;
    if (hook_name == "on_fixed_simulation") {
      const auto dt_seconds = std::chrono::duration_cast<seconds_f>(
        context->GetFixedDeltaTime().get())
                                .count();
      lua_pushnumber(lua_state_, dt_seconds);
      ++arg_count;
    } else if (hook_name == "on_gameplay") {
      const auto dt_seconds = std::chrono::duration_cast<seconds_f>(
        context->GetGameDeltaTime().get())
                                .count();
      lua_pushnumber(lua_state_, dt_seconds);
      ++arg_count;
    }
  }

  lua_pushcfunction(lua_state_, LuaTraceback, kLuaTracebackFnName);
  lua_insert(lua_state_, kLuaTracebackIndex); // [ traceback, fn, args... ]
  const auto call_status
    = lua_pcall(lua_state_, arg_count, kLuaNoResults, kLuaTracebackIndex);
  if (call_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaErrorAndTracebackCount); // error + traceback
    return ErrorResult("phase_hook", error_message);
  }
  lua_pop(lua_state_, kLuaSingleValueCount); // traceback

  return OkResult();
}

auto ScriptingModule::RunSceneScripts(
  observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("runtime", "lua state is null");
  }
  if (context == nullptr) {
    return OkResult();
  }

  const auto scene = context->GetScene();
  if (scene == nullptr) {
    CleanupStaleSlotRuntimes({});
    return OkResult();
  }

  using seconds_f = std::chrono::duration<float>;
  const float dt_seconds
    = std::chrono::duration_cast<seconds_f>(context->GetGameDeltaTime().get())
        .count();

  std::unordered_set<SlotRuntimeKey, SlotRuntimeKeyHash> active_keys;
  auto traversal = scene->Traverse();
  [[maybe_unused]] const auto traversal_result = traversal.Traverse(
    [&](const auto& visited_node, const bool dry_run) -> scene::VisitResult {
      if (dry_run || visited_node.node_impl == nullptr) {
        return scene::VisitResult::kContinue;
      }

      auto node = scene->GetNode(visited_node.handle);
      if (!node.has_value()) {
        return scene::VisitResult::kContinue;
      }
      if (!node->HasScripting()) {
        return scene::VisitResult::kContinue;
      }

      auto scripting = node->GetScripting();
      const auto slots = scripting.Slots();
      for (size_t i = 0; i < slots.size(); ++i) {
        const auto& slot = slots[i];
        if (slot.State()
            != scene::ScriptingComponent::Slot::CompileState::kReady
          || slot.IsDisabled() || slot.Executable() == nullptr) {
          continue;
        }

        SlotRuntimeKey key {
          .node_handle = node->GetHandle(),
          .slot_index = static_cast<uint32_t>(i),
        };
        active_keys.insert(key);

        auto& runtime = slot_runtimes_[key];
        if (runtime.executable != slot.Executable()) {
          DestroySlotRuntime(runtime);
          runtime.executable = slot.Executable();
          const auto init_result = RebuildSlotRuntime(key, runtime, slot);
          if (!init_result.ok && !runtime.reported_initialization_error) {
            runtime.reported_initialization_error = true;
            const auto msg = std::string("script slot initialization failed [")
                               .append(init_result.stage)
                               .append("]: ")
                               .append(init_result.message);
            LOG_SCOPE_F(ERROR, "Script Init Error");
            LOG_F(ERROR, "    stage: {}", init_result.stage);
            LOG_F(ERROR, "  message: {}", init_result.message);
            ReportError(context, msg);
          }
          if (!init_result.ok) {
            continue;
          }
        }

        const auto tick_result
          = ExecuteSlotTick(key, runtime, *node, slot, context, dt_seconds);
        if (!tick_result.ok) {
          const auto msg = std::string("script slot tick failed [")
                             .append(tick_result.stage)
                             .append("]: ")
                             .append(tick_result.message);
          LOG_SCOPE_F(ERROR, "Script Tick Error");
          LOG_F(ERROR, "    stage: {}", tick_result.stage);
          LOG_F(ERROR, "  message: {}", tick_result.message);
          ReportError(context, msg);
        }
      }

      return scene::VisitResult::kContinue;
    });

  CleanupStaleSlotRuntimes(active_keys);
  return OkResult();
}

auto ScriptingModule::ExecuteSlotTick(const SlotRuntimeKey& key,
  SlotRuntimeState& runtime_state, const scene::SceneNode& node,
  const scene::ScriptingComponent::Slot& slot,
  const observer_ptr<engine::FrameContext> context, const float dt_seconds)
  -> ScriptExecutionResult
{
  const ScopedActiveFrameContext active_context(lua_state_, context);

  if (runtime_state.failed_initialization) {
    return ErrorResult("slot_binding", runtime_state.initialization_error);
  }

  if (!IsValidLuaRef(runtime_state.tick_ref)) {
    const auto rebuild_result = RebuildSlotRuntime(key, runtime_state, slot);
    if (!rebuild_result.ok) {
      return rebuild_result;
    }
  }

  if (!IsValidLuaRef(runtime_state.tick_ref)) {
    return ErrorResult("slot_binding", "missing tick(ctx, dt) entry point");
  }

  lua_getref(lua_state_, runtime_state.tick_ref);
  if (!lua_isfunction(lua_state_, kLuaStackTop)) {
    lua_pop(lua_state_, kLuaSingleValueCount);
    return ErrorResult("slot_binding", "tick entry point is not callable");
  }

  bindings::LuaSlotExecutionContext invocation_context { .node = node,
    .slot = &slot };
  bindings::PushScriptContext(lua_state_, &invocation_context, dt_seconds);
  lua_pushnumber(lua_state_, dt_seconds);

  lua_pushcfunction(lua_state_, LuaTraceback, kLuaTracebackFnName);
  lua_insert(lua_state_, kLuaTracebackIndex); // [ traceback, fn, ctx, dt ]
  const auto call_status = lua_pcall(lua_state_, 2, kLuaNoResults, 1);
  if (call_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaErrorAndTracebackCount); // error + traceback
    return ErrorResult("slot_runtime", error_message);
  }

  lua_pop(lua_state_, kLuaSingleValueCount); // traceback
  return OkResult();
}

auto ScriptingModule::RebuildSlotRuntime(const SlotRuntimeKey& key,
  SlotRuntimeState& state, const scene::ScriptingComponent::Slot& slot)
  -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("runtime", "lua state is null");
  }

  DestroySlotRuntime(state);
  state.executable = slot.Executable();
  state.failed_initialization = false;
  state.reported_initialization_error = false;
  state.initialization_error.clear();

  if (state.executable == nullptr) {
    return ErrorResult("slot_binding", "slot has no executable");
  }

  const auto bytecode = state.executable->BytecodeView();
  if (bytecode.empty()) {
    return ErrorResult(
      "slot_binding", "slot executable has no bytecode payload");
  }

  lua_getref(lua_state_, runtime_env_ref_);
  const auto env_index = lua_gettop(lua_state_);
  const std::string chunk_name = "scene_slot_" + std::to_string(key.slot_index);
  const auto load_status = luau_load(lua_state_, chunk_name.c_str(),
    // NOLINTNEXTLINE(*-reinterpret-cast)
    reinterpret_cast<const char*>(bytecode.data()), bytecode.size(), env_index);
  if (load_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaEnvironmentAndErrorCount); // error + env
    state.failed_initialization = true;
    state.initialization_error = error_message;
    return ErrorResult("slot_load", error_message);
  }
  lua_remove(lua_state_, env_index); // keep chunk only

  lua_pushcfunction(lua_state_, LuaTraceback, kLuaTracebackFnName);
  const auto chunk_index = lua_gettop(lua_state_) - 1;
  lua_insert(lua_state_, chunk_index); // [ traceback, chunk ]
  const auto call_status = lua_pcall(lua_state_, kLuaNoArgs, 1, chunk_index);
  if (call_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaErrorAndTracebackCount); // error + traceback
    state.failed_initialization = true;
    state.initialization_error = error_message;
    return ErrorResult("slot_runtime", error_message);
  }

  lua_remove(lua_state_, -2); // remove traceback, keep return value

  if (lua_isfunction(lua_state_, kLuaStackTop)) {
    state.tick_ref = lua_ref(lua_state_, kLuaStackTop); // pops function
    state.module_ref = kLuaNoRef;
    return OkResult();
  }

  if (lua_istable(lua_state_, kLuaStackTop)) {
    const auto value_index = lua_gettop(lua_state_);
    state.module_ref = lua_ref(lua_state_, value_index); // pops table
    if (!IsValidLuaRef(state.module_ref)) {
      state.failed_initialization = true;
      state.initialization_error = "script returned nil module table";
      return ErrorResult("slot_binding", state.initialization_error);
    }

    lua_getref(lua_state_, state.module_ref);
    lua_getfield(lua_state_, kLuaStackTop, "tick");
    if (!lua_isfunction(lua_state_, kLuaStackTop)) {
      lua_pop(lua_state_, 2); // non-function + module table
      state.failed_initialization = true;
      state.initialization_error = "script module must expose tick(ctx, dt)";
      return ErrorResult("slot_binding", state.initialization_error);
    }
    state.tick_ref = lua_ref(lua_state_, kLuaStackTop); // pops function
    lua_pop(lua_state_, kLuaSingleValueCount); // module table
    return OkResult();
  }

  lua_pop(lua_state_, kLuaSingleValueCount); // unexpected return value
  state.failed_initialization = true;
  state.initialization_error
    = "script must return either a function or a module table";
  return ErrorResult("slot_binding", state.initialization_error);
}

auto ScriptingModule::DestroySlotRuntime(SlotRuntimeState& state) -> void
{
  if (lua_state_ != nullptr) {
    if (IsValidLuaRef(state.tick_ref)) {
      lua_unref(lua_state_, state.tick_ref);
    }
    if (IsValidLuaRef(state.module_ref)) {
      lua_unref(lua_state_, state.module_ref);
    }
  }

  state.tick_ref = kLuaNoRef;
  state.module_ref = kLuaNoRef;
  state.failed_initialization = false;
  state.reported_initialization_error = false;
  state.initialization_error.clear();
}

auto ScriptingModule::CleanupStaleSlotRuntimes(
  const std::unordered_set<SlotRuntimeKey, SlotRuntimeKeyHash>& active_keys)
  -> void
{
  for (auto it = slot_runtimes_.begin(); it != slot_runtimes_.end();) {
    if (active_keys.contains(it->first)) {
      ++it;
      continue;
    }

    DestroySlotRuntime(it->second);
    it = slot_runtimes_.erase(it);
  }
}

auto ScriptingModule::ReportHookError(
  observer_ptr<engine::FrameContext> context, const std::string_view hook_name,
  const ScriptExecutionResult& result) const -> void
{
  if (context == nullptr) {
    return;
  }

  const auto msg = std::string("script phase hook '")
                     .append(hook_name)
                     .append("' failed [")
                     .append(result.stage)
                     .append("]: ")
                     .append(result.message);

  LOG_SCOPE_F(ERROR, "Script Hook Error");
  LOG_F(ERROR, "hook_name: {}", hook_name);
  LOG_F(ERROR, "    stage: {}", result.stage);
  LOG_F(ERROR, "  message: {}", result.message);

  ReportError(context, msg);
}

} // namespace oxygen::scripting
