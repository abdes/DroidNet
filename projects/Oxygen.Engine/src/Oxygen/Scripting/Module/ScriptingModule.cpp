//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
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
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentAsyncBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/CoreBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Input/InputBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindingPack.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

namespace oxygen::scripting {

namespace {
  constexpr int kLuaNoArgs = 0;
  constexpr int kLuaNoResults = 0;
  constexpr int kLuaStackTop = -1;
  constexpr int kLuaStackSecondTop = -2;
  constexpr int kLuaSingleValueCount = 1;

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

  class ScopedLuaStackTop final {
  public:
    explicit ScopedLuaStackTop(
      lua_State* state, const char* scope_name) noexcept
      : state_(state)
      , top_(state != nullptr ? lua_gettop(state) : 0)
      , scope_name_(scope_name)
    {
    }

    ~ScopedLuaStackTop()
    {
      if (state_ != nullptr) {
        const int current_top = lua_gettop(state_);
        CHECK_F(current_top == top_,
          "lua stack imbalance in {}: entry_top={} exit_top={}", scope_name_,
          top_, current_top);
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(ScopedLuaStackTop)
    OXYGEN_DEFAULT_MOVABLE(ScopedLuaStackTop)

  private:
    lua_State* state_ { nullptr };
    int top_ { 0 };
    const char* scope_name_ { "unknown_scope" };
  };

  auto LuaToString(lua_State* state, int index) -> std::string
  {
    if (const auto* text = lua_tostring(state, index); text != nullptr) {
      return text;
    }
    return "unknown lua error";
  }

  auto BuildLuaErrorWithTraceback(lua_State* state) -> std::string
  {
    // Preserve original error text when available, then append traceback
    // without relying on lua_pcall message handlers.
    std::string message = LuaToString(state, kLuaStackTop);
    luaL_traceback(state, state, message.c_str(), 1);
    auto with_traceback = LuaToString(state, kLuaStackTop);
    lua_pop(state, 1); // traceback string
    return with_traceback;
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

  auto CreateRefPreserveStack(lua_State* state, const int index) -> int
  {
    const int top = lua_gettop(state);
    const int abs_index = lua_absindex(state, index);
    lua_pushvalue(state, abs_index);
    const int ref = lua_ref(state, kLuaStackTop);
    lua_settop(state, top);
    return ref;
  }

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
      // Freeze the library (math, table, etc.) if it is a table
      lua_getfield(state, kLuaStackTop, global_name);
      if (lua_istable(state, -1)) {
        lua_setreadonly(state, -1, 1);
      }
      lua_pop(state, 1);
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
  : priority_(priority)
{
}

ScriptingModule::~ScriptingModule() { OnShutdown(); }

auto ScriptingModule::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
  -> bool
{
  CHECK_NOTNULL_F(engine);
  engine_ = engine;
  attached_ = true;
  (void)engine_->GetScriptCompilationService().RegisterCompiler(
    std::make_shared<LuauScriptCompiler>());

  if (auto loader = engine_->GetAssetLoader()) {
    script_reload_subscription_ = loader->SubscribeScriptReload(
      [this](const data::AssetKey& key,
        const std::shared_ptr<const data::ScriptResource>& resource) {
        if (!resource) {
          return;
        }

        // Wrap data::ScriptResource into scripting::ScriptBytecodeBlob
        const auto& data = resource->GetData();
        std::vector<uint8_t> bytes(data.begin(), data.end());
        auto bytecode = std::make_shared<const ScriptBytecodeBlob>(
          ScriptBytecodeBlob::FromOwned(std::move(bytes),
            resource->GetLanguage(), resource->GetCompression(),
            resource->GetContentHash(), ScriptBlobOrigin::kExternalFile,
            ScriptBlobCanonicalName { "hot-reload" }));

        LOG_F(INFO, "applying hot-reload for asset {} (new_hash={})",
          data::to_string(key), bytecode->ContentHash());

        // Find all instances using this asset and update their executable
        // bytecode. The next observer sync/update will detect executable
        // change and rebuild runtimes.
        for (auto& [slot_key, state] : slot_runtimes_) {
          if (state.asset_key == key && state.executable
            && slot_key.node_handle.IsValid()) {
            // Downcast to CompiledScriptExecutable to update bytecode
            // NOLINTNEXTLINE(*-static-cast-downcast)
            auto* compiled = static_cast<CompiledScriptExecutable*>(
              // NOLINTNEXTLINE(*-const-cast)
              const_cast<ScriptExecutable*>(state.executable.get()));
            compiled->UpdateBytecode(bytecode);
          }
        }
      });
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
  if (auto observed_scene = observed_scene_.lock(); observed_scene != nullptr) {
    (void)observed_scene->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
  }
  observed_scene_.reset();

  if (attached_) {
    DCHECK_NOTNULL_F(engine_);
    (void)engine_->GetScriptCompilationService().UnregisterCompiler(
      data::pak::scripting::ScriptLanguage::kLuau);
    attached_ = false;
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

    if (global_env_ref_ != LUA_NOREF) {
      lua_unref(lua_state_, global_env_ref_);
      global_env_ref_ = LUA_NOREF;
    }

    if (runtime_env_ref_ != LUA_NOREF) {
      lua_unref(lua_state_, runtime_env_ref_);
      runtime_env_ref_ = LUA_NOREF;
    }

    lua_close(lua_state_);
    lua_state_ = nullptr;
  }

  slot_runtimes_.clear();
  active_frame_slots_.clear();
  active_slot_indices_.clear();
  task_queue_.EndSession();
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

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = "scripting.reload_all",
    .help = "Manually trigger a full reload of all script assets",
    .handler = [this](const auto&, const auto&) -> console::ExecutionResult {
      if (!attached_) {
        return {
          .status = console::ExecutionStatus::kError,
          .error = "Engine not attached",
        };
      }
      DCHECK_NOTNULL_F(engine_);
      if (auto loader = engine_->GetAssetLoader()) {
        loader->ReloadAllScripts();
        return { .status = console::ExecutionStatus::kOk,
          .output = "Reloading all scripts..." };
      }
      return {
        .status = console::ExecutionStatus::kError,
        .error = "AssetLoader not available",
      };
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = "scripting.list_roots",
    .help = "List all registered script source roots",
    .handler = [this](const auto&, const auto&) -> console::ExecutionResult {
      if (!attached_) {
        return { .status = console::ExecutionStatus::kError,
          .error = "Engine not attached" };
      }
      DCHECK_NOTNULL_F(engine_);
      const auto roots = engine_->GetPathFinder().ScriptSourceRoots();
      std::string output = "Registered Script Source Roots:\n";
      for (const auto& root : roots) {
        output += fmt::format("  - {} ({})\n", root.generic_string(),
          std::filesystem::exists(root) ? "active" : "missing");
      }
      return {
        .status = console::ExecutionStatus::kOk,
        .output = output,
      };
    },
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
    bindings::CreatePhysicsBindingPack(),
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
    ProcessPendingTasks();
    const ScopedActiveFrameContext active_context(lua_state_, context);
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
    const ScopedActiveFrameContext active_context(lua_state_, context);
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
  EnsureSceneObservation(context);

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
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
    const ScopedActiveFrameContext active_context(lua_state_, context);
    bindings::SetActiveEventPhase(lua_state_, "scene_mutation");
  }

  const auto result = InvokePhaseHook("on_scene_mutation", context);
  if (!result.ok) {
    ReportHookError(context, "on_scene_mutation", result);
  }

  const auto scene_mutation_result = RunSceneMutationScripts(context);
  if (!scene_mutation_result.ok) {
    ReportHookError(context, "scene_mutation", scene_mutation_result);
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
    const ScopedActiveFrameContext active_context(lua_state_, context);
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
  const ScopedLuaStackTop stack_guard(lua_state_, "ExecuteScript(request)");

  const Luau::CompileOptions options {
    .optimizationLevel = 1,
    .debugLevel = 2,
  };
  const std::string bytecode
    = Luau::compile(std::string(request.source_text.get()), options);

  lua_getref(lua_state_, global_env_ref_);
  const auto env_index = lua_gettop(lua_state_);

  const std::string chunk_name_string { request.chunk_name.get() };
  const auto load_status = luau_load(lua_state_, chunk_name_string.c_str(),
    bytecode.data(), bytecode.size(), env_index);
  if (load_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, 2); // error + global_env
    return ErrorResult("compile_or_load", error_message);
  }
  lua_remove(lua_state_, env_index); // remove global_env
  // stack now contains only the loaded chunk

  const auto call_status = lua_pcall(lua_state_, kLuaNoArgs, kLuaNoResults, 0);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in ExecuteScript(request)");
  if (call_status != LUA_OK) {
    const auto error_message = BuildLuaErrorWithTraceback(lua_state_);
    LOG_F(ERROR, "ExecuteScript failed at runtime: {}", error_message);
    lua_pop(lua_state_, 1); // original error object
    return ErrorResult("runtime", error_message);
  }

  return OkResult();
}

auto ScriptingModule::InitializeSandbox() -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("bootstrap", "lua state is null");
  }

  luaL_openlibs(lua_state_);
  luaL_sandbox(lua_state_);
  runtime_env_ref_ = CreateRuntimeEnvironment(lua_state_);

  task_queue_.StartSession();

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

  // Freeze the 'oxygen' table in the environment
  lua_getref(lua_state_, runtime_env_ref_);
  lua_getfield(lua_state_, -1, "oxygen");
  if (lua_istable(lua_state_, -1)) {
    lua_setreadonly(lua_state_, -1, 1);
  }
  lua_pop(lua_state_, 1);

  // Finally freeze the entire environment table
  lua_setreadonly(lua_state_, -1, 1);
  lua_pop(lua_state_, 1);

  // Create the shared Global Environment for ExecuteScript and as base for
  // slots
  lua_newtable(lua_state_); // global_env
  lua_newtable(lua_state_); // mt
  lua_getref(lua_state_, runtime_env_ref_);
  lua_setfield(lua_state_, -2, "__index");
  lua_setmetatable(lua_state_, -2);

  lua_pushvalue(lua_state_, -1);
  lua_setfield(lua_state_, -2, "_G"); // global_env._G = global_env

  global_env_ref_ = lua_ref(lua_state_, -1);
  lua_settop(lua_state_, 0);

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

auto ScriptingModule::SubmitMainThreadTask(
  Task task, ScriptingSessionId session_id) -> void
{
  task_queue_.Submit(std::move(task), session_id);
}

auto ScriptingModule::GetSessionId() const -> ScriptingSessionId
{
  return task_queue_.GetSessionId();
}

auto ScriptingModule::ProcessPendingTasks() -> void
{
  if (lua_state_ == nullptr) {
    return;
  }

  // Set active engine context for tasks that might need it
  const ScopedActiveFrameContext active_context(lua_state_, nullptr);
  task_queue_.Process(lua_state_);
}

auto ScriptingModule::InvokePhaseHook(const std::string_view hook_name,
  observer_ptr<engine::FrameContext> context) -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("runtime", "lua state is null");
  }
  const ScopedLuaStackTop stack_guard(lua_state_, "InvokePhaseHook");
  const int hook_entry_top = lua_gettop(lua_state_);

  const bool found_env_hook
    = TryGetHookFunctionFromEnvironment(lua_state_, global_env_ref_, hook_name);
  if (found_env_hook) {
    CHECK_F(lua_gettop(lua_state_) == hook_entry_top + 1,
      "InvokePhaseHook stack contract violated after env hook lookup "
      "(hook={}, entry_top={}, current_top={})",
      hook_name, hook_entry_top, lua_gettop(lua_state_));
  } else {
    CHECK_F(lua_gettop(lua_state_) == hook_entry_top,
      "InvokePhaseHook stack contract violated after env miss "
      "(hook={}, entry_top={}, current_top={})",
      hook_name, hook_entry_top, lua_gettop(lua_state_));

    const bool found_global_hook = TryGetHookFunction(lua_state_, hook_name);
    if (!found_global_hook) {
      CHECK_F(lua_gettop(lua_state_) == hook_entry_top,
        "InvokePhaseHook stack contract violated after global miss "
        "(hook={}, entry_top={}, current_top={})",
        hook_name, hook_entry_top, lua_gettop(lua_state_));
      return OkResult();
    }
    CHECK_F(lua_gettop(lua_state_) == hook_entry_top + 1,
      "InvokePhaseHook stack contract violated after global hook lookup "
      "(hook={}, entry_top={}, current_top={})",
      hook_name, hook_entry_top, lua_gettop(lua_state_));
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

  CHECK_F(lua_gettop(lua_state_) == hook_entry_top + 1 + arg_count,
    "InvokePhaseHook pre-pcall stack mismatch (hook={}, entry_top={}, "
    "arg_count={}, current_top={})",
    hook_name, hook_entry_top, arg_count, lua_gettop(lua_state_));

  const auto call_status = lua_pcall(lua_state_, arg_count, kLuaNoResults, 0);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in InvokePhaseHook(hook={})", hook_name);
  if (call_status != LUA_OK) {
    CHECK_F(lua_gettop(lua_state_) >= hook_entry_top + 1,
      "InvokePhaseHook error stack missing error object (hook={}, "
      "entry_top={}, "
      "current_top={})",
      hook_name, hook_entry_top, lua_gettop(lua_state_));
    const auto error_message = BuildLuaErrorWithTraceback(lua_state_);
    lua_pop(lua_state_, 1); // original error object
    return ErrorResult("phase_hook", error_message);
  }

  CHECK_F(lua_gettop(lua_state_) == hook_entry_top,
    "InvokePhaseHook post-pcall stack mismatch (hook={}, entry_top={}, "
    "current_top={})",
    hook_name, hook_entry_top, lua_gettop(lua_state_));

  return OkResult();
}

auto ScriptingModule::OnScriptSlotActivated(
  const scene::NodeHandle& node_handle, const scene::ScriptSlotIndex slot_index,
  const scene::ScriptingComponent::Slot& slot) noexcept -> void
{
  ActivateSlot(
    SlotRuntimeKey { .node_handle = node_handle, .slot_index = slot_index },
    slot);
}

auto ScriptingModule::OnScriptSlotChanged(const scene::NodeHandle& node_handle,
  const scene::ScriptSlotIndex slot_index,
  const scene::ScriptingComponent::Slot& slot) noexcept -> void
{
  UpdateSlot(
    SlotRuntimeKey { .node_handle = node_handle, .slot_index = slot_index },
    slot);
}

auto ScriptingModule::OnScriptSlotDeactivated(
  const scene::NodeHandle& node_handle,
  const scene::ScriptSlotIndex slot_index) noexcept -> void
{
  DeactivateSlot(
    SlotRuntimeKey { .node_handle = node_handle, .slot_index = slot_index });
}

auto ScriptingModule::EnsureSceneObservation(
  const observer_ptr<engine::FrameContext> context) -> void
{
  const auto scene = (context != nullptr) ? context->GetScene() : nullptr;
  const auto observed_scene = observed_scene_.lock();
  if (scene.get() == observed_scene.get()) {
    return;
  }

  if (observed_scene != nullptr) {
    (void)observed_scene->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
  }

  observed_scene_.reset();
  if (scene != nullptr) {
    observed_scene_ = scene->weak_from_this();
  }
  active_frame_slots_.clear();
  active_slot_indices_.clear();

  for (auto& [_, runtime] : slot_runtimes_) {
    DestroySlotRuntime(runtime);
  }
  slot_runtimes_.clear();

  if (scene != nullptr) {
    const bool subscribed
      = scene->RegisterObserver(observer_ptr<scene::ISceneObserver> { this });
    LOG_F(INFO, "scene mutation observer {}",
      subscribed ? "subscribed" : "already subscribed");

    // Observer registration receives only future mutations; seed active slots
    // from the current scene snapshot so pre-existing ready slots execute on
    // the next gameplay tick.
    auto nodes_to_visit = scene->GetRootNodes();
    while (!nodes_to_visit.empty()) {
      auto node = std::move(nodes_to_visit.back());
      nodes_to_visit.pop_back();

      if (!node.IsAlive()) {
        continue;
      }

      if (node.HasScripting()) {
        const auto scripting = node.GetScripting();
        const auto slots = scripting.Slots();
        constexpr auto kMaxSlotIndex = (std::numeric_limits<uint32_t>::max)();
        if (slots.size() > kMaxSlotIndex) {
          LOG_F(ERROR,
            "Skipping scripting slot hydration for node {}: slot count {} "
            "exceeds uint32 range",
            nostd::to_string(node.GetHandle()), slots.size());
        } else {
          for (size_t i = 0; i < slots.size(); ++i) {
            const auto& slot = slots[i];
            if (slot.Executable() == nullptr || slot.IsDisabled()) {
              continue;
            }

            ActivateSlot(
              SlotRuntimeKey { .node_handle = node.GetHandle(),
                .slot_index
                = scene::ScriptSlotIndex { static_cast<uint32_t>(i) } },
              slot);
          }
        }
      }

      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        nodes_to_visit.push_back(*child);
      }
    }
  }
}

auto ScriptingModule::ActivateSlot(const SlotRuntimeKey& key,
  const scene::ScriptingComponent::Slot& slot) -> void
{
  if (slot.Executable() == nullptr) {
    return;
  }

  if (const auto it = active_slot_indices_.find(key);
    it != active_slot_indices_.end()) {
    auto& active_slot = active_frame_slots_[it->second];
    active_slot.executable = slot.Executable();
    active_slot.executable_hash = slot.Executable()->ContentHash();
    return;
  }

  const auto index = active_frame_slots_.size();
  active_slot_indices_.insert_or_assign(key, index);
  active_frame_slots_.push_back(ActiveScriptSlot {
    .key = key,
    .executable = slot.Executable(),
    .executable_hash = slot.Executable()->ContentHash(),
  });
}

auto ScriptingModule::UpdateSlot(const SlotRuntimeKey& key,
  const scene::ScriptingComponent::Slot& slot) -> void
{
  if (slot.Executable() == nullptr) {
    DeactivateSlot(key);
    return;
  }

  ActivateSlot(key, slot);

  if (const auto runtime_it = slot_runtimes_.find(key);
    runtime_it != slot_runtimes_.end()) {
    auto& runtime = runtime_it->second;
    if (runtime.executable != slot.Executable()
      || runtime.last_known_hash != slot.Executable()->ContentHash()) {
      DestroySlotRuntime(runtime);
      runtime.executable = slot.Executable();
      runtime.last_known_hash = slot.Executable()->ContentHash();
    }
  }
}

auto ScriptingModule::DeactivateSlot(const SlotRuntimeKey& key) -> void
{
  const auto idx_it = active_slot_indices_.find(key);
  if (idx_it != active_slot_indices_.end()) {
    const size_t index = idx_it->second;
    const size_t last_index = active_frame_slots_.size() - 1;
    if (index != last_index) {
      active_frame_slots_[index] = std::move(active_frame_slots_[last_index]);
      active_slot_indices_[active_frame_slots_[index].key] = index;
    }
    active_frame_slots_.pop_back();
    active_slot_indices_.erase(idx_it);
  }

  const auto runtime_it = slot_runtimes_.find(key);
  if (runtime_it != slot_runtimes_.end()) {
    DestroySlotRuntime(runtime_it->second);
    slot_runtimes_.erase(runtime_it);
  }
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
    return OkResult();
  }

  using seconds_f = std::chrono::duration<float>;
  const float dt_seconds
    = std::chrono::duration_cast<seconds_f>(context->GetGameDeltaTime().get())
        .count();

  for (const auto& slot_info : active_frame_slots_) {
    // Re-validate node existence (handle safety)
    auto node = scene->GetNode(slot_info.key.node_handle);
    if (!node.has_value() || !node->HasScripting()) {
      continue;
    }

    auto scripting = node->GetScripting();
    const auto slots = scripting.Slots();
    if (slot_info.key.slot_index >= slots.size()) {
      continue;
    }

    const auto& slot = slots[slot_info.key.slot_index];
    if (slot.IsDisabled() || slot.Executable() == nullptr) {
      continue;
    }

    auto& runtime = slot_runtimes_[slot_info.key];
    const bool runtime_needs_rebuild
      = runtime.executable != slot_info.executable
      || runtime.last_known_hash != slot_info.executable_hash;

    if (runtime_needs_rebuild) {
      DestroySlotRuntime(runtime);
      runtime.executable = slot_info.executable;
      runtime.last_known_hash = slot_info.executable_hash;
      const auto init_result = RebuildSlotRuntime(slot_info.key, runtime, slot);
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

    const auto gameplay_result = ExecuteSlotGameplay(
      slot_info.key, runtime, *node, slot, context, dt_seconds);
    if (!gameplay_result.ok) {
      const auto msg = std::string("script slot on_gameplay failed [")
                         .append(gameplay_result.stage)
                         .append("]: ")
                         .append(gameplay_result.message);
      LOG_SCOPE_F(ERROR, "Script Slot Error");
      LOG_F(ERROR, "    stage: {}", gameplay_result.stage);
      LOG_F(ERROR, "  message: {}", gameplay_result.message);
      ReportError(context, msg);
    }
  }

  return OkResult();
}

auto ScriptingModule::RunSceneMutationScripts(
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
    return OkResult();
  }

  using seconds_f = std::chrono::duration<float>;
  const float dt_seconds
    = std::chrono::duration_cast<seconds_f>(context->GetGameDeltaTime().get())
        .count();

  for (const auto& slot_info : active_frame_slots_) {
    // Re-validate node existence (handle safety)
    auto node = scene->GetNode(slot_info.key.node_handle);
    if (!node.has_value() || !node->HasScripting()) {
      continue;
    }

    auto scripting = node->GetScripting();
    const auto slots = scripting.Slots();
    if (slot_info.key.slot_index >= slots.size()) {
      continue;
    }

    const auto& slot = slots[slot_info.key.slot_index];
    if (slot.IsDisabled() || slot.Executable() == nullptr) {
      continue;
    }

    auto& runtime = slot_runtimes_[slot_info.key];
    const bool runtime_needs_rebuild
      = runtime.executable != slot_info.executable
      || runtime.last_known_hash != slot_info.executable_hash;

    if (runtime_needs_rebuild) {
      DestroySlotRuntime(runtime);
      runtime.executable = slot_info.executable;
      runtime.last_known_hash = slot_info.executable_hash;
      const auto init_result = RebuildSlotRuntime(slot_info.key, runtime, slot);
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

    const auto mutation_result = ExecuteSlotSceneMutation(
      slot_info.key, runtime, *node, slot, context, dt_seconds);
    if (!mutation_result.ok) {
      const auto msg = std::string("script slot on_scene_mutation failed [")
                         .append(mutation_result.stage)
                         .append("]: ")
                         .append(mutation_result.message);
      LOG_SCOPE_F(ERROR, "Script Slot Error");
      LOG_F(ERROR, "    stage: {}", mutation_result.stage);
      LOG_F(ERROR, "  message: {}", mutation_result.message);
      ReportError(context, msg);
    }
  }

  return OkResult();
}

auto ScriptingModule::ExecuteSlotGameplay(const SlotRuntimeKey& key,
  SlotRuntimeState& runtime_state, const scene::SceneNode& node,
  const scene::ScriptingComponent::Slot& slot,
  const observer_ptr<engine::FrameContext> context, const float dt_seconds)
  -> ScriptExecutionResult
{
  const ScopedLuaStackTop stack_guard(lua_state_, "ExecuteSlotGameplay");
  const ScopedActiveFrameContext active_context(lua_state_, context);

  if (runtime_state.failed_initialization) {
    return ErrorResult("slot_binding", runtime_state.initialization_error);
  }

  if (!IsValidLuaRef(runtime_state.on_gameplay_ref)
    && !IsValidLuaRef(runtime_state.on_scene_mutation_ref)) {
    const auto rebuild_result = RebuildSlotRuntime(key, runtime_state, slot);
    if (!rebuild_result.ok) {
      return rebuild_result;
    }
  }

  if (!IsValidLuaRef(runtime_state.on_gameplay_ref)) {
    return OkResult();
  }

  lua_getref(lua_state_, runtime_state.on_gameplay_ref);
  if (!lua_isfunction(lua_state_, kLuaStackTop)) {
    lua_pop(lua_state_, kLuaSingleValueCount);
    return ErrorResult(
      "slot_binding", "on_gameplay entry point is not callable");
  }

  bindings::LuaSlotExecutionContext invocation_context {
    .node_handle = node.GetHandle(), .slot = &slot
  };
  bindings::PushScriptContext(lua_state_, &invocation_context, dt_seconds);
  lua_pushnumber(lua_state_, dt_seconds);

  const auto call_status = lua_pcall(lua_state_, 2, kLuaNoResults, 0);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in ExecuteSlotGameplay");
  if (call_status != LUA_OK) {
    const auto error_message = BuildLuaErrorWithTraceback(lua_state_);
    lua_pop(lua_state_, 1); // original error object
    return ErrorResult("slot_runtime", error_message);
  }
  return OkResult();
}

auto ScriptingModule::ExecuteSlotSceneMutation(const SlotRuntimeKey& key,
  SlotRuntimeState& runtime_state, const scene::SceneNode& node,
  const scene::ScriptingComponent::Slot& slot,
  const observer_ptr<engine::FrameContext> context, const float dt_seconds)
  -> ScriptExecutionResult
{
  const ScopedLuaStackTop stack_guard(lua_state_, "ExecuteSlotSceneMutation");
  const ScopedActiveFrameContext active_context(lua_state_, context);

  if (runtime_state.failed_initialization) {
    return ErrorResult("slot_binding", runtime_state.initialization_error);
  }

  if (!IsValidLuaRef(runtime_state.on_gameplay_ref)
    && !IsValidLuaRef(runtime_state.on_scene_mutation_ref)) {
    const auto rebuild_result = RebuildSlotRuntime(key, runtime_state, slot);
    if (!rebuild_result.ok) {
      return rebuild_result;
    }
  }

  if (!IsValidLuaRef(runtime_state.on_scene_mutation_ref)) {
    return OkResult();
  }

  lua_getref(lua_state_, runtime_state.on_scene_mutation_ref);
  if (!lua_isfunction(lua_state_, kLuaStackTop)) {
    lua_pop(lua_state_, kLuaSingleValueCount);
    return ErrorResult(
      "slot_binding", "on_scene_mutation entry point is not callable");
  }

  bindings::LuaSlotExecutionContext invocation_context {
    .node_handle = node.GetHandle(), .slot = &slot
  };
  bindings::PushScriptContext(lua_state_, &invocation_context, dt_seconds);
  lua_pushnumber(lua_state_, dt_seconds);

  const auto call_status = lua_pcall(lua_state_, 2, kLuaNoResults, 0);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in ExecuteSlotSceneMutation");
  if (call_status != LUA_OK) {
    const auto error_message = BuildLuaErrorWithTraceback(lua_state_);
    lua_pop(lua_state_, 1); // original error object
    return ErrorResult("slot_runtime", error_message);
  }
  return OkResult();
}

auto ScriptingModule::RebuildSlotRuntime(const SlotRuntimeKey& key,
  SlotRuntimeState& state, const scene::ScriptingComponent::Slot& slot)
  -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("runtime", "lua state is null");
  }
  const ScopedLuaStackTop stack_guard(lua_state_, "RebuildSlotRuntime");

  DestroySlotRuntime(state);
  state.executable = slot.Executable();
  state.asset_key
    = slot.Asset() ? slot.Asset()->GetAssetKey() : data::AssetKey {};
  state.failed_initialization = false;
  state.reported_initialization_error = false;
  state.initialization_error.clear();

  if (state.executable == nullptr) {
    return ErrorResult("slot_binding", "slot has no executable");
  }
  state.last_known_hash = state.executable->ContentHash();

  InitializeInstanceEnvironment(state);
  if (state.instance_env_ref == LUA_NOREF) {
    return ErrorResult("slot_binding", "failed to create instance environment");
  }

  const auto bytecode = state.executable->BytecodeView();
  if (bytecode.empty()) {
    return ErrorResult(
      "slot_binding", "slot executable has no bytecode payload");
  }

  lua_getref(lua_state_, state.instance_env_ref);
  const auto env_index = lua_gettop(lua_state_);
  const std::string chunk_name = "scene_slot_" + std::to_string(key.slot_index);
  const auto load_status = luau_load(lua_state_, chunk_name.c_str(),
    // NOLINTNEXTLINE(*-reinterpret-cast)
    reinterpret_cast<const char*>(bytecode.data()), bytecode.size(), env_index);
  if (load_status != LUA_OK) {
    const auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_settop(lua_state_, env_index - 1); // drop env and load error
    state.failed_initialization = true;
    state.initialization_error = error_message;
    return ErrorResult("slot_load", error_message);
  }
  lua_remove(lua_state_, env_index); // keep chunk only

  const auto call_status = lua_pcall(lua_state_, kLuaNoArgs, 1, 0);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in RebuildSlotRuntime");
  if (call_status != LUA_OK) {
    const auto error_message = BuildLuaErrorWithTraceback(lua_state_);
    lua_pop(lua_state_, 1); // original error object
    state.failed_initialization = true;
    state.initialization_error = error_message;
    return ErrorResult("slot_runtime", error_message);
  }

  if (lua_isfunction(lua_state_, kLuaStackTop)) {
    state.on_gameplay_ref = CreateRefPreserveStack(lua_state_, kLuaStackTop);
    lua_pop(lua_state_, kLuaSingleValueCount); // function
    state.on_scene_mutation_ref = kLuaNoRef;
    state.module_ref = kLuaNoRef;
    return OkResult();
  }

  if (lua_istable(lua_state_, kLuaStackTop)) {
    const auto value_index = lua_gettop(lua_state_);
    state.module_ref = CreateRefPreserveStack(lua_state_, value_index);
    lua_pop(lua_state_, kLuaSingleValueCount); // returned module table
    if (!IsValidLuaRef(state.module_ref)) {
      state.failed_initialization = true;
      state.initialization_error = "script returned nil module table";
      return ErrorResult("slot_binding", state.initialization_error);
    }

    lua_getref(lua_state_, state.module_ref);
    if (!lua_istable(lua_state_, kLuaStackTop)) {
      lua_pop(lua_state_, kLuaSingleValueCount);
      state.failed_initialization = true;
      state.initialization_error = "script module reference is not a table";
      return ErrorResult("slot_binding", state.initialization_error);
    }
    const int module_table_index = lua_gettop(lua_state_);

    lua_getfield(lua_state_, module_table_index, "on_gameplay");
    if (lua_isfunction(lua_state_, kLuaStackTop)) {
      state.on_gameplay_ref = CreateRefPreserveStack(lua_state_, kLuaStackTop);
      lua_pop(lua_state_, kLuaSingleValueCount); // function
    } else {
      lua_pop(lua_state_, kLuaSingleValueCount); // non-function
      state.on_gameplay_ref = kLuaNoRef;
    }

    lua_getfield(lua_state_, module_table_index, "on_scene_mutation");
    if (lua_isfunction(lua_state_, kLuaStackTop)) {
      state.on_scene_mutation_ref
        = CreateRefPreserveStack(lua_state_, kLuaStackTop);
      lua_pop(lua_state_, kLuaSingleValueCount); // function
    } else {
      lua_pop(lua_state_, kLuaSingleValueCount); // non-function
      state.on_scene_mutation_ref = kLuaNoRef;
    }

    if (!IsValidLuaRef(state.on_gameplay_ref)
      && !IsValidLuaRef(state.on_scene_mutation_ref)) {
      lua_pop(lua_state_, kLuaSingleValueCount); // module table
      state.failed_initialization = true;
      state.initialization_error = "script module must expose on_gameplay(ctx, "
                                   "dt) or on_scene_mutation(ctx, dt)";
      return ErrorResult("slot_binding", state.initialization_error);
    }

    lua_pop(lua_state_, kLuaSingleValueCount); // module table
    return OkResult();
  }

  lua_pop(lua_state_, kLuaSingleValueCount); // unexpected return value
  state.failed_initialization = true;
  state.initialization_error
    = "script must return either a function or a module table";
  return ErrorResult("slot_binding", state.initialization_error);
}

auto ScriptingModule::InitializeInstanceEnvironment(SlotRuntimeState& state)
  -> void
{
  if (lua_state_ == nullptr || global_env_ref_ == LUA_NOREF) {
    return;
  }

  lua_newtable(lua_state_); // instance_env

  // Create metatable for inheritance from sandbox globals
  lua_newtable(lua_state_); // mt
  lua_getref(lua_state_, global_env_ref_);
  lua_setfield(lua_state_, -2, "__index");

  lua_setmetatable(lua_state_, -2);

  state.instance_env_ref = CreateRefPreserveStack(lua_state_, -1);
  lua_pop(lua_state_, 1);
}

auto ScriptingModule::DestroySlotRuntime(SlotRuntimeState& state) -> void
{
  if (lua_state_ != nullptr) {
    if (IsValidLuaRef(state.on_gameplay_ref)) {
      lua_unref(lua_state_, state.on_gameplay_ref);
    }
    if (IsValidLuaRef(state.on_scene_mutation_ref)) {
      lua_unref(lua_state_, state.on_scene_mutation_ref);
    }
    if (IsValidLuaRef(state.module_ref)) {
      lua_unref(lua_state_, state.module_ref);
    }
    if (IsValidLuaRef(state.instance_env_ref)) {
      lua_unref(lua_state_, state.instance_env_ref);
    }
  }

  state.on_gameplay_ref = kLuaNoRef;
  state.on_scene_mutation_ref = kLuaNoRef;
  state.module_ref = kLuaNoRef;
  state.instance_env_ref = kLuaNoRef;
  state.last_known_hash = 0;
  state.failed_initialization = false;
  state.reported_initialization_error = false;
  state.initialization_error.clear();
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
