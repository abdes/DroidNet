//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <Luau/Compiler.h>
#include <fmt/format.h>
#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/ScriptSlotIndex.h>
#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/LuauDefinitions.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ScriptDeclarationBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>
#include <Oxygen/Scripting/Module/RuntimeDiagnostics.h>
#include <Oxygen/Scripting/Module/ScriptSourceReload.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

namespace oxygen::scripting {

namespace {
  constexpr int kLuaNoArgs = 0;
  constexpr int kLuaNoResults = 0;
  constexpr int kLuaStackTop = -1;
  constexpr int kLuaStackSecondTop = -2;
  constexpr int kLuaSingleValueCount = 1;

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

  //! A slot diagnostic without a source location.
  auto SlotDiagnostic(const DiagnosticCode code, std::string message,
    std::vector<std::string> notes = {}) -> ScriptDiagnostic
  {
    return ScriptDiagnostic {
      .severity = DiagnosticSeverity::kError,
      .code = code,
      .message = std::move(message),
      .labels = {},
      .notes = std::move(notes),
      .help = std::nullopt,
      .slot = std::nullopt,
    };
  }

  //! Chunk name of a slot script, without `@`: its source path, else its
  //! asset name, else its asset key.
  auto SlotSourceName(const scene::ScriptingComponent::Slot& slot)
    -> std::string
  {
    const auto& asset = slot.Asset();
    if (!asset) {
      return "script";
    }
    if (const auto path = asset->TryGetExternalSourcePath(); path) {
      return std::string(*path);
    }
    if (const auto name = asset->GetAssetName(); !name.empty()) {
      return std::string(name);
    }
    return data::to_string(asset->GetAssetKey());
  }

  //! True when `name` is a node or asset parameter of `slot` without a value.
  auto IsUnsetReference(const scene::ScriptingComponent::Slot& slot,
    const std::string_view name) -> bool
  {
    for (const auto& entry : slot.Parameters()) {
      if (entry.key != name) {
        continue;
      }
      const auto& value = entry.value.get();
      if (const auto* node = std::get_if<data::ScriptNodeParam>(&value)) {
        return node->node_index == data::pak::scripting::kUnsetNodeParam;
      }
      if (const auto* asset = std::get_if<data::ScriptAssetParam>(&value)) {
        return asset->key == data::AssetKey {};
      }
      return false;
    }
    return false;
  }

  auto ExecutableFingerprint(const ScriptExecutable& executable) -> uint64_t
  {
    if (const auto hash = executable.ContentHash(); hash != 0) {
      return hash;
    }
    // Compiler output and cooked resources may omit the authored hash. This
    // runtime comparison fingerprint still detects in-place bytecode reloads;
    // it does not change resource metadata or its validation contract.
    const auto bytecode = executable.BytecodeView();
    return ComputeFNV1a64(bytecode.data(), bytecode.size());
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

  //! A slot's private environment; globals fall back to the shared global
  //! environment.
  auto CreateInstanceEnvironment(lua_State* state, const int global_env_ref)
    -> int
  {
    lua_newtable(state); // instance_env
    lua_newtable(state); // metatable
    lua_getref(state, global_env_ref);
    lua_setfield(state, kLuaStackSecondTop, "__index");
    lua_setmetatable(state, kLuaStackSecondTop);
    const auto ref = lua_ref(state, kLuaStackTop);
    lua_pop(state, kLuaSingleValueCount);
    return ref;
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

  // Changed script sources recompile into the slots of the observed scene.
  source_reload_
    = std::make_unique<ScriptSourceReload>(engine_->GetPathFinder(),
      observer_ptr { &engine_->GetScriptCompilationService() },
      [this](const ScriptDiagnostic& diagnostic) -> void {
        PublishSourceDiagnostic(diagnostic);
      });
  engine_->SetScriptSourceChangedHandler(
    [this](const std::filesystem::path& file) -> void {
      (void)source_reload_->OnSourceChanged(observed_scene_.lock(), file);
    });

  if (!CreateRuntime()) {
    OnShutdown();
    return false;
  }
  return true;
}

auto ScriptingModule::CreateRuntime() -> bool
{
  lua_state_ = luaL_newstate();
  if (lua_state_ == nullptr) {
    return false;
  }
  bindings::SetActiveEngine(lua_state_, engine_);

  if (binding_packs_.empty() && !RegisterDefaultBindingPacks()) {
    return false;
  }

  const auto sandbox_result = InitializeSandbox();
  if (!sandbox_result.ok) {
    DLOG_F(ERROR, "scripting bootstrap failed: {}", sandbox_result.message);
    return false;
  }
  return true;
}

auto ScriptingModule::DestroyRuntime() noexcept -> void
{
  if (auto observed_scene = observed_scene_.lock(); observed_scene != nullptr) {
    (void)observed_scene->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
  }
  observed_scene_.reset();

  if (lua_state_ != nullptr) {
    bindings::SetActiveFrameContext(lua_state_, {});
    // Instances end while the engine bindings are still reachable.
    EndAllInstances();
    bindings::SetActiveEngine(lua_state_, {});
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
  active_slots_sorted_ = true;
  error_frames_.clear();
  task_queue_.Drain();
}

auto ScriptingModule::OnSessionEnd(
  [[maybe_unused]] observer_ptr<IAsyncEngine> engine) noexcept -> void
{
  // A session leaves no instance, global or pending task behind; diagnostic
  // handlers, binding packs and the compilation caches survive.
  DestroyRuntime();
  try {
    if (attached_ && !CreateRuntime()) {
      LOG_F(ERROR, "scripting runtime could not be recreated after a session");
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "scripting runtime recreation failed: {}", ex.what());
  }
}

auto ScriptingModule::OnShutdown() noexcept -> void
{
  // Instances end while the engine is still attached.
  DestroyRuntime();

  if (attached_) {
    DCHECK_NOTNULL_F(engine_);
    engine_->SetScriptSourceChangedHandler({});
    source_reload_.reset();
    (void)engine_->GetScriptCompilationService().UnregisterCompiler(
      data::pak::scripting::ScriptLanguage::kLuau);
    attached_ = false;
    engine_ = nullptr;
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

auto ScriptingModule::RegisterCommands(
  const observer_ptr<commands::CommandRegistry> registry) noexcept -> void
{
  if (registry == nullptr) {
    return;
  }
  static_cast<void>(registry->Register(commands::CommandDefinition {
    .name = "scripting.reload_all",
    .label = "Reload All Scripts",
    .help = "Recompile every script of the scene from its source",
    .handler = [this](const commands::CommandArgs&,
                 const commands::CommandContext&) -> commands::CommandResult {
      if (!attached_ || !source_reload_) {
        return {
          .status = commands::ExecutionStatus::kError,
          .error = "Engine not attached",
        };
      }
      const auto submitted = source_reload_->ReloadAll(observed_scene_.lock());
      return {
        .status = commands::ExecutionStatus::kOk,
        .output = fmt::format("Recompiling {} script(s)...", submitted),
      };
    },
  }));

  static_cast<void>(registry->Register(commands::CommandDefinition {
    .name = "scripting.list_roots",
    .label = "List Script Roots",
    .help = "List all registered script source roots",
    .handler = [this](const commands::CommandArgs&,
                 const commands::CommandContext&) -> commands::CommandResult {
      if (!attached_) {
        return {
          .status = commands::ExecutionStatus::kError,
          .error = "Engine not attached",
        };
      }
      DCHECK_NOTNULL_F(engine_);
      const auto roots = engine_->GetPathFinder().ScriptSourceRoots();
      std::string output = "Registered Script Source Roots:\n";
      for (const auto& root : roots) {
        output += fmt::format("  - {} ({})\n", root.generic_string(),
          std::filesystem::exists(root) ? "active" : "missing");
      }
      return {
        .status = commands::ExecutionStatus::kOk,
        .output = output,
      };
    },
  }));
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
  for (auto& pack : bindings::CreateDefaultBindingPacks()) {
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
  EnsureSceneObservation(context);
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
    ReportEventDispatchErrors(context, "frame_start", dispatch_result);
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
  RunInstances(context, InstanceHook::kFixedSimulation);

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "fixed_simulation");
    ReportEventDispatchErrors(context, "fixed_simulation", dispatch_result);
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

  RunInstances(context, InstanceHook::kGameplay);

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "gameplay");
    ReportEventDispatchErrors(context, "gameplay", dispatch_result);
  }

  co_return;
}

auto ScriptingModule::OnSceneMutation(
  observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  EnsureSceneObservation(context);
  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    bindings::SetActiveEventPhase(lua_state_, "scene_mutation");
  }

  const auto result = InvokePhaseHook("on_scene_mutation", context);
  if (!result.ok) {
    ReportHookError(context, "on_scene_mutation", result);
  }

  RunInstances(context, InstanceHook::kSceneMutation);

  if (lua_state_ != nullptr) {
    const ScopedActiveFrameContext active_context(lua_state_, context);
    const auto dispatch_result
      = bindings::DispatchEventsForPhase(lua_state_, "scene_mutation");
    ReportEventDispatchErrors(context, "scene_mutation", dispatch_result);
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
    ReportEventDispatchErrors(context, "frame_end", dispatch_result);
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

  const auto already_registered = std::ranges::any_of(
    binding_packs_, [&pack](const auto& existing) -> bool {
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
      std::erase_if(binding_packs_, [raw_pack](const auto& item) -> bool {
        return item.get() == raw_pack;
      });
      return false;
    }
  }
  return true;
}

auto ScriptingModule::UnregisterBindingPack(std::string_view pack_name) -> bool
{
  const auto old_size = binding_packs_.size();
  std::erase_if(binding_packs_, [pack_name](const auto& pack) -> bool {
    return pack && pack->Name() == pack_name;
  });
  return old_size != binding_packs_.size();
}

auto ScriptingModule::SubmitMainThreadTask(
  Task task, ScriptingSessionId session_id) -> void
{
  task_queue_.Submit(std::move(task), session_id);
}

auto ScriptingModule::AddDiagnosticHandler(ScriptDiagnosticHandler handler)
  -> ScriptDiagnosticHandlerId
{
  const auto id = ScriptDiagnosticHandlerId { next_diagnostic_handler_id_++ };
  diagnostic_handlers_.emplace_back(id, std::move(handler));
  return id;
}

auto ScriptingModule::RemoveDiagnosticHandler(
  const ScriptDiagnosticHandlerId id) -> bool
{
  return std::erase_if(diagnostic_handlers_, [id](const auto& entry) -> bool {
    return entry.first == id;
  }) > 0;
}

auto ScriptingModule::SetDiagnosticSources(
  std::shared_ptr<const SourceProvider> sources) -> void
{
  diagnostic_sources_ = std::move(sources);
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
  // A seeded runtime may precede the mutation dispatcher's first activation
  // notification. Reconcile its executable now, including old owner retirement.
  UpdateSlot(
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
  active_slots_sorted_ = true;
  EndAllInstances();

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
        constexpr auto kMaxSlotIndex = std::numeric_limits<uint32_t>::max();
        if (slots.size() > kMaxSlotIndex) {
          LOG_F(ERROR,
            "Skipping scripting slot hydration for node {}: slot count {} "
            "exceeds uint32 range",
            nostd::to_string(node.GetHandle()), slots.size());
        } else {
          for (size_t i = 0; i < slots.size(); ++i) {
            const auto& slot = slots.subspan(i, 1).front();
            if (slot.Executable() == nullptr || slot.IsDisabled()) {
              continue;
            }

            ActivateSlot(
              SlotRuntimeKey {
                .node_handle = node.GetHandle(),
                .slot_index
                = scene::ScriptSlotIndex { static_cast<uint32_t>(i) },
              },
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
    auto& active_slot = active_frame_slots_.at(it->second);
    if (active_slot.execution_order != slot.ExecutionOrder()) {
      active_slot.execution_order = slot.ExecutionOrder();
      active_slots_sorted_ = false;
    }
    return;
  }

  active_slot_indices_.insert_or_assign(key, active_frame_slots_.size());
  active_frame_slots_.push_back(ActiveScriptSlot {
    .key = key,
    .execution_order = slot.ExecutionOrder(),
  });
  active_slots_sorted_ = false;
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
      || runtime.last_known_fingerprint
        != ExecutableFingerprint(*slot.Executable())
      || !IsCurrentSlotRuntime(key, runtime)) {
      // The next phase creates a new instance from the new executable.
      EndInstance(runtime);
      slot_runtimes_.erase(runtime_it);
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
      active_frame_slots_.at(index) = active_frame_slots_.at(last_index);
      active_slot_indices_[active_frame_slots_.at(index).key] = index;
      active_slots_sorted_ = false;
    }
    active_frame_slots_.pop_back();
    active_slot_indices_.erase(idx_it);
  }

  const auto runtime_it = slot_runtimes_.find(key);
  if (runtime_it != slot_runtimes_.end()) {
    EndInstance(runtime_it->second);
    slot_runtimes_.erase(runtime_it);
  }
}

auto ScriptingModule::SortActiveSlots() -> void
{
  if (active_slots_sorted_) {
    return;
  }
  const auto order = [](const ActiveScriptSlot& slot)
    -> std::tuple<int, unsigned int, unsigned int> {
    return { slot.execution_order, slot.key.node_handle.Index(),
      slot.key.slot_index.get() };
  };
  std::ranges::sort(
    active_frame_slots_, [&order](const auto& lhs, const auto& rhs) -> auto {
      return order(lhs) < order(rhs);
    });
  for (size_t i = 0; i < active_frame_slots_.size(); ++i) {
    active_slot_indices_[active_frame_slots_.at(i).key] = i;
  }
  active_slots_sorted_ = true;
}

auto ScriptingModule::EndAllInstances() -> void
{
  for (auto& [_, runtime] : slot_runtimes_) {
    EndInstance(runtime);
  }
  slot_runtimes_.clear();
}

auto ScriptingModule::RunInstances(
  const observer_ptr<engine::FrameContext> context, const InstanceHook hook)
  -> void
{
  if (lua_state_ == nullptr || context == nullptr
    || context->GetScene() == nullptr) {
    return;
  }

  SortActiveSlots();
  // Hooks may change the active set; iterate a stable copy of the order.
  std::vector<SlotRuntimeKey> keys;
  keys.reserve(active_frame_slots_.size());
  for (const auto& active_slot : active_frame_slots_) {
    keys.push_back(active_slot.key);
  }

  using seconds_f = std::chrono::duration<float>;
  const auto delta = hook == InstanceHook::kFixedSimulation
    ? context->GetFixedDeltaTime().get()
    : context->GetGameDeltaTime().get();
  const float dt_seconds = std::chrono::duration_cast<seconds_f>(delta).count();
  const ScopedActiveFrameContext active_context(lua_state_, context);

  if (hook == InstanceHook::kGameplay) {
    // Every instance ready this phase starts before any on_gameplay runs.
    for (const auto& key : keys) {
      auto* runtime = PrepareInstance(key, context);
      if (runtime == nullptr || runtime->started) {
        continue;
      }
      if (auto failure = CallInstanceHook(*runtime, "on_start", std::nullopt)) {
        FaultInstance(*runtime, context, "on_start", std::move(*failure));
      } else {
        runtime->started = true;
      }
    }
  }

  const auto hook_name = [hook] -> std::string_view {
    switch (hook) {
    case InstanceHook::kFixedSimulation:
      return "on_fixed_simulation";
    case InstanceHook::kGameplay:
      return "on_gameplay";
    case InstanceHook::kSceneMutation:
      return "on_scene_mutation";
    }
    return {};
  }();
  for (const auto& key : keys) {
    auto* runtime = PrepareInstance(key, context);
    if (runtime == nullptr || !runtime->started) {
      continue;
    }
    if (auto failure = CallInstanceHook(*runtime, hook_name, dt_seconds)) {
      FaultInstance(*runtime, context, hook_name, std::move(*failure));
    }
  }
}

auto ScriptingModule::PrepareInstance(const SlotRuntimeKey& key,
  const observer_ptr<engine::FrameContext> context) -> SlotRuntimeState*
{
  const auto scene = context->GetScene();
  auto node = scene->GetNode(key.node_handle);
  if (!node.has_value() || !node->HasScripting()) {
    return nullptr;
  }
  const auto slots = node->GetScripting().Slots();
  if (key.slot_index >= slots.size()) {
    return nullptr;
  }
  const auto& slot = slots.subspan(key.slot_index.get(), 1).front();
  if (slot.IsDisabled() || slot.Executable() == nullptr) {
    return nullptr;
  }

  // In-place reloads and same-hash executable replacements need not emit
  // scene slot mutations. Reconcile against the attached slot every phase.
  auto& runtime = slot_runtimes_[key];
  if (runtime.executable != slot.Executable()
    || runtime.last_known_fingerprint
      != ExecutableFingerprint(*slot.Executable())
    || !IsCurrentSlotRuntime(key, runtime)) {
    EndInstance(runtime);
    if (auto failure = CreateInstance(key, runtime, *node, slot)) {
      FaultInstance(runtime, context, "load", std::move(*failure));
    }
  }
  return runtime.faulted ? nullptr : &runtime;
}

auto ScriptingModule::CreateInstance(const SlotRuntimeKey& key,
  SlotRuntimeState& state, scene::SceneNode& node,
  const scene::ScriptingComponent::Slot& slot)
  -> std::optional<ScriptDiagnostic>
{
  const ScopedLuaStackTop stack_guard(lua_state_, "CreateInstance");

  state.executable = slot.Executable();
  state.last_known_fingerprint = ExecutableFingerprint(*state.executable);
  state.asset_key
    = slot.Asset() ? slot.Asset()->GetAssetKey() : data::AssetKey {};
  state.source_name = SlotSourceName(slot);
  state.node_name = std::string(node.GetName());
  state.slot_index = static_cast<uint32_t>(key.slot_index.get());
  state.slot_identity = slot;
  const auto owner_impl = node.GetImpl();
  if (!owner_impl) {
    return SlotDiagnostic(
      DiagnosticCode::kLoadFailed, "the node that owns the slot is destroyed");
  }
  const auto incarnation = owner_impl->get()
                             .GetComponent<scene::ScriptingComponent>()
                             .IncarnationId();
  state.component_incarnation = incarnation;
  state.event_owner_id = bindings::CreateEventListenerOwner(lua_state_,
    [scene = observed_scene_, key, incarnation, slot_identity = slot,
      executable = std::weak_ptr<const ScriptExecutable>(state.executable),
      fingerprint = state.last_known_fingerprint] -> bool {
      const auto owner_scene = scene.lock();
      const auto expected_executable = executable.lock();
      if (!owner_scene || !expected_executable) {
        return false;
      }
      auto owner = owner_scene->GetNode(key.node_handle);
      if (!owner || !owner->HasScripting()) {
        return false;
      }
      const auto impl = owner->GetImpl();
      if (!impl) {
        return false;
      }
      const auto& component
        = impl->get().GetComponent<scene::ScriptingComponent>();
      if (component.IncarnationId() != incarnation) {
        return false;
      }
      const auto index = component.TryGetSlotIndex(slot_identity);
      if (!index || *index != key.slot_index) {
        return false;
      }
      const auto slots = component.Slots();
      if (index->get() >= slots.size()) {
        return false;
      }
      const auto& current = slots.subspan(index->get(), 1U).front();
      return !current.IsDisabled()
        && current.Executable() == expected_executable
        && ExecutableFingerprint(*current.Executable()) == fingerprint;
    });
  const bindings::ScopedEventListenerOwner owner_scope(
    lua_state_, state.event_owner_id);

  state.instance_env_ref
    = CreateInstanceEnvironment(lua_state_, global_env_ref_);
  const auto bytecode = state.executable->BytecodeView();
  if (bytecode.empty()) {
    return SlotDiagnostic(DiagnosticCode::kLoadFailed,
      "the slot's executable has no bytecode",
      { fmt::format("script: {}", state.source_name) });
  }

  detail::PushErrorFrameCollector(lua_state_, &error_frames_);
  const auto handler_index = lua_gettop(lua_state_);
  lua_getref(lua_state_, state.instance_env_ref);
  const auto env_index = lua_gettop(lua_state_);
  const auto chunk_name = "@" + state.source_name;
  const auto load_status = luau_load(lua_state_, chunk_name.c_str(),
    // NOLINTNEXTLINE(*-reinterpret-cast)
    reinterpret_cast<const char*>(bytecode.data()), bytecode.size(), env_index);
  if (load_status != LUA_OK) {
    auto error_message = LuaToString(lua_state_, kLuaStackTop);
    lua_settop(lua_state_, handler_index - 1); // drop handler, env and error
    return SlotDiagnostic(DiagnosticCode::kLoadFailed,
      "the script bytecode could not be loaded",
      {
        std::move(error_message),
        fmt::format("script: {}", state.source_name),
      });
  }
  lua_remove(lua_state_, env_index); // keep chunk only

  const auto call_status = lua_pcall(lua_state_, kLuaNoArgs, 1, handler_index);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in CreateInstance");
  if (call_status != LUA_OK) {
    auto diagnostic = RuntimeDiagnostic(state, "load");
    lua_pop(lua_state_, 2); // error object and handler
    return diagnostic;
  }
  lua_remove(lua_state_, handler_index); // keep the returned value
  if (!bindings::IsScriptTable(lua_state_, kLuaStackTop)) {
    lua_pop(lua_state_, 1); // returned value
    auto diagnostic = SlotDiagnostic(DiagnosticCode::kNotAScript,
      "the script does not return its `oxygen.script` table",
      { fmt::format("script: {}", state.source_name) });
    diagnostic.help = Suggestion {
      .message = "end the script with `return` of the table created by "
                 "`oxygen.script`",
      .span = std::nullopt,
      .replacement = std::nullopt,
    };
    return diagnostic;
  }
  state.script_ref = lua_ref(lua_state_, kLuaStackTop);
  lua_pop(lua_state_, 1); // script table

  lua_newtable(lua_state_); // self
  bindings::PushSceneNode(lua_state_, node);
  lua_setfield(lua_state_, kLuaStackSecondTop, "node");
  PushInstanceParams(slot);
  lua_setfield(lua_state_, kLuaStackSecondTop, "params");
  lua_newtable(lua_state_); // metatable: methods resolve on the script
  lua_getref(lua_state_, state.script_ref);
  lua_setfield(lua_state_, kLuaStackSecondTop, "__index");
  lua_setmetatable(lua_state_, kLuaStackSecondTop);
  state.self_ref = lua_ref(lua_state_, kLuaStackTop);
  lua_pop(lua_state_, 1); // self
  return std::nullopt;
}

auto ScriptingModule::PushInstanceParams(
  const scene::ScriptingComponent::Slot& slot) -> void
{
  lua_newtable(lua_state_);
  const auto scene = observed_scene_.lock();
  for (const auto& entry : slot.Parameters()) {
    lua_pushlstring(lua_state_, entry.key.data(), entry.key.size());
    // A node parameter is the scene node it was bound to; unbound, or bound
    // to a node that no longer exists, it is nil.
    if (std::holds_alternative<data::ScriptNodeParam>(entry.value.get())) {
      const auto bound = slot.TryGetBoundNode(entry.key);
      const auto target = bound.has_value() && scene != nullptr
        ? scene->GetNode(*bound)
        : std::nullopt;
      if (target.has_value()) {
        (void)bindings::PushSceneNode(lua_state_, *target);
      } else {
        lua_pushnil(lua_state_);
      }
    } else {
      (void)bindings::PushScriptParam(lua_state_, entry.value.get());
    }
    lua_rawset(lua_state_, -3);
  }
  lua_setreadonly(lua_state_, kLuaStackTop, 1);
}

auto ScriptingModule::CallInstanceHook(SlotRuntimeState& state,
  const std::string_view hook_name, const std::optional<float> dt_seconds)
  -> std::optional<ScriptDiagnostic>
{
  const ScopedLuaStackTop stack_guard(lua_state_, "CallInstanceHook");
  if (!IsValidLuaRef(state.script_ref) || !IsValidLuaRef(state.self_ref)) {
    return SlotDiagnostic(
      DiagnosticCode::kLoadFailed, "the script instance is not loaded");
  }

  const bindings::ScopedEventListenerOwner owner_scope(
    lua_state_, state.event_owner_id);
  detail::PushErrorFrameCollector(lua_state_, &error_frames_);
  const auto handler_index = lua_gettop(lua_state_);
  lua_getref(lua_state_, state.script_ref);
  lua_getfield(lua_state_, kLuaStackTop, std::string(hook_name).c_str());
  lua_remove(lua_state_, kLuaStackSecondTop); // keep the hook
  if (!lua_isfunction(lua_state_, kLuaStackTop)) {
    lua_pop(lua_state_, 2); // non-function and handler
    return std::nullopt; // hooks are optional
  }

  lua_getref(lua_state_, state.self_ref);
  int arg_count = 1;
  if (dt_seconds.has_value()) {
    lua_pushnumber(lua_state_, *dt_seconds);
    ++arg_count;
  }
  const auto call_status
    = lua_pcall(lua_state_, arg_count, kLuaNoResults, handler_index);
  CHECK_F(call_status != LUA_ERRERR,
    "lua_pcall returned LUA_ERRERR in CallInstanceHook");
  if (call_status != LUA_OK) {
    auto diagnostic = RuntimeDiagnostic(state, hook_name);
    lua_pop(lua_state_, 2); // error object and handler
    return diagnostic;
  }
  lua_pop(lua_state_, 1); // handler
  return std::nullopt;
}

auto ScriptingModule::RuntimeDiagnostic(const SlotRuntimeState& state,
  const std::string_view hook_name) -> ScriptDiagnostic
{
  const auto error_message = LuaToString(lua_state_, kLuaStackTop);
  return detail::MakeRuntimeDiagnostic(detail::RuntimeErrorInput {
    .error_message = error_message,
    .frames = error_frames_,
    .entry_name = hook_name,
    .sources = diagnostic_sources_.get(),
    .is_unset_reference = [&state](const std::string_view name) -> bool {
      return state.slot_identity.has_value()
        && IsUnsetReference(*state.slot_identity, name);
    },
  });
}

auto ScriptingModule::FaultInstance(SlotRuntimeState& state,
  const observer_ptr<engine::FrameContext> context,
  const std::string_view hook_name, ScriptDiagnostic diagnostic) -> void
{
  state.faulted = true;
  bindings::RetireEventListenerOwner(lua_state_, state.event_owner_id);
  PublishDiagnostic(state, hook_name, std::move(diagnostic), context);
}

auto ScriptingModule::PublishDiagnostic(const SlotRuntimeState& state,
  const std::string_view hook_name, ScriptDiagnostic diagnostic,
  const observer_ptr<engine::FrameContext> context) -> void
{
  diagnostic.slot = SlotContext {
    .node = state.node_name,
    .slot_index = state.slot_index,
    .hook = std::string(hook_name),
  };
  static const auto kNoSources = SourceTextCache {};
  auto text = RenderText(
    diagnostic, diagnostic_sources_ ? *diagnostic_sources_ : kNoSources);
  if (text.ends_with('\n')) {
    text.pop_back();
  }
  LOG_F(ERROR, "{}", text);
  if (context) {
    ReportContentError(context, FormatSummary(diagnostic));
  }
  // A handler may add or remove handlers.
  const auto handlers = diagnostic_handlers_;
  for (const auto& handler : handlers | std::views::values) {
    handler(diagnostic);
  }
}

auto ScriptingModule::PublishSourceDiagnostic(
  const ScriptDiagnostic& diagnostic) -> void
{
  static const auto kNoSources = SourceTextCache {};
  auto text = RenderText(
    diagnostic, diagnostic_sources_ ? *diagnostic_sources_ : kNoSources);
  if (text.ends_with('\n')) {
    text.pop_back();
  }
  LOG_F(ERROR, "{}", text);
  // A handler may add or remove handlers.
  const auto handlers = diagnostic_handlers_;
  for (const auto& handler : handlers | std::views::values) {
    handler(diagnostic);
  }
}

auto ScriptingModule::EndInstance(SlotRuntimeState& state) -> void
{
  if (lua_state_ != nullptr) {
    if (state.started && !state.faulted) {
      // The node may already be gone; on_destroy sees self.node as it is.
      if (auto failure = CallInstanceHook(state, "on_destroy", std::nullopt)) {
        PublishDiagnostic(state, "on_destroy", std::move(*failure), nullptr);
      }
    }
    // Listeners registered by the instance, on_destroy included, end here.
    bindings::RetireEventListenerOwner(lua_state_, state.event_owner_id);
    for (const auto ref :
      { state.self_ref, state.script_ref, state.instance_env_ref }) {
      if (IsValidLuaRef(ref)) {
        lua_unref(lua_state_, ref);
      }
    }
  }
  state = SlotRuntimeState {};
}

auto ScriptingModule::IsCurrentSlotRuntime(
  const SlotRuntimeKey& key, const SlotRuntimeState& state) const -> bool
{
  if (!state.slot_identity || state.component_incarnation.IsNil()) {
    return false;
  }
  const auto scene = observed_scene_.lock();
  if (!scene) {
    return false;
  }
  auto node = scene->GetNode(key.node_handle);
  if (!node || !node->HasScripting()) {
    return false;
  }
  const auto impl = node->GetImpl();
  if (!impl) {
    return false;
  }
  const auto& component = impl->get().GetComponent<scene::ScriptingComponent>();
  return component.IncarnationId() == state.component_incarnation
    && component.TryGetSlotIndex(*state.slot_identity) == key.slot_index;
}

auto ScriptingModule::ReportEventDispatchErrors(
  observer_ptr<engine::FrameContext> context, const std::string_view phase,
  const bindings::EventDispatchStatus& status) -> void
{
  if (status.ok || context == nullptr) {
    return;
  }
  const auto prefix = std::string("oxygen.events dispatch failed [")
                        .append(phase)
                        .append("]: ");
  if (status.failures.empty()) {
    LOG_F(ERROR, "{}{}", prefix, status.message);
    ReportError(context, prefix + status.message);
    return;
  }
  bool global_error_reported = false;
  for (const auto& failure : status.failures) {
    if (failure.owner == 0) {
      if (!global_error_reported) {
        LOG_F(ERROR, "{}{}", prefix, failure.message);
        ReportError(context, prefix + failure.message);
        global_error_reported = true;
      }
      continue;
    }
    auto message = prefix + failure.message;
    for (auto& [key, runtime] : slot_runtimes_) {
      if (runtime.event_owner_id == failure.owner) {
        runtime.faulted = true;
        message.append(" (node=")
          .append(nostd::to_string(key.node_handle))
          .append(" slot=")
          .append(std::to_string(key.slot_index.get()))
          .append(")");
        break;
      }
    }
    bindings::RetireEventListenerOwner(lua_state_, failure.owner);
    LOG_F(ERROR, "{}", message);
    ReportContentError(context, message);
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
