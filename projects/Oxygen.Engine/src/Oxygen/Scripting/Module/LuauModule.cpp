//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <string>
#include <utility>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Module/LuauModule.h>

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

LuauModule::LuauModule(const engine::ModulePriority priority)
  : runtime_env_ref_(LUA_NOREF)
  , priority_(priority)
{
}

LuauModule::~LuauModule() { OnShutdown(); }

auto LuauModule::OnAttached(observer_ptr<AsyncEngine> /*engine*/) noexcept
  -> bool
{
  lua_state_ = luaL_newstate();
  if (lua_state_ == nullptr) {
    return false;
  }

  const auto sandbox_result = InitializeSandbox();
  if (!sandbox_result.ok) {
    DLOG_F(ERROR, "LuauModule bootstrap failed: {}", sandbox_result.message);
    OnShutdown();
    return false;
  }

  return true;
}

auto LuauModule::OnShutdown() noexcept -> void
{
  if (lua_state_ != nullptr) {
    if (runtime_env_ref_ != LUA_NOREF) {
      lua_unref(lua_state_, runtime_env_ref_);
      runtime_env_ref_ = LUA_NOREF;
    }
    lua_close(lua_state_);
    lua_state_ = nullptr;
  }
}

auto LuauModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  const auto result = InvokePhaseHook("on_frame_start", context);
  if (!result.ok) {
    ReportHookError(context, "on_frame_start", result);
  }
}

auto LuauModule::OnFixedSimulation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  const auto result = InvokePhaseHook("on_fixed_simulation", context);
  if (!result.ok) {
    ReportHookError(context, "on_fixed_simulation", result);
  }
  co_return;
}

auto LuauModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  const auto result = InvokePhaseHook("on_gameplay", context);
  if (!result.ok) {
    ReportHookError(context, "on_gameplay", result);
  }
  co_return;
}

auto LuauModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  const auto result = InvokePhaseHook("on_scene_mutation", context);
  if (!result.ok) {
    ReportHookError(context, "on_scene_mutation", result);
  }
  co_return;
}

auto LuauModule::OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void
{
  const auto result = InvokePhaseHook("on_frame_end", context);
  if (!result.ok) {
    ReportHookError(context, "on_frame_end", result);
  }
}

auto LuauModule::ExecuteScript(const ScriptExecutionRequest& request)
  -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult(
      "runtime", "cannot execute script: luau module is not attached");
  }

  Luau::CompileOptions options;
  options.optimizationLevel = 1;
  options.debugLevel = 1;
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

auto LuauModule::ExecuteScript(const ::oxygen::scripting::IScriptLoader& loader,
  const std::string_view script_id) -> ScriptExecutionResult
{
  const auto loaded = loader.LoadScript(script_id);
  if (!loaded.ok) {
    return ErrorResult("load", loaded.error_message);
  }

  return ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { loaded.source_text },
    .chunk_name = ScriptChunkName { loaded.chunk_name },
  });
}

auto LuauModule::InitializeSandbox() -> ScriptExecutionResult
{
  if (lua_state_ == nullptr) {
    return ErrorResult("bootstrap", "lua state is null");
  }

  luaL_openlibs(lua_state_);
  luaL_sandbox(lua_state_);
  runtime_env_ref_ = CreateRuntimeEnvironment(lua_state_);
  return OkResult();
}

auto LuauModule::InvokePhaseHook(const std::string_view hook_name,
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

auto LuauModule::ReportHookError(observer_ptr<engine::FrameContext> context,
  const std::string_view hook_name, const ScriptExecutionResult& result) const
  -> void
{
  if (context == nullptr) {
    return;
  }

  ReportError(context,
    std::string("Luau phase hook '")
      .append(hook_name)
      .append("' failed [")
      .append(result.stage)
      .append("]: ")
      .append(result.message));
}

} // namespace oxygen::scripting
