//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/TimeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto RequireFrameContext(lua_State* state)
    -> observer_ptr<engine::FrameContext>
  {
    const auto frame_context = GetActiveFrameContext();
    if (frame_context == nullptr) {
      (void)luaL_error(state, "oxygen.time requires active FrameContext");
      return nullptr;
    }
    return frame_context;
  }

  auto ContextDeltaSeconds(lua_State* state) -> float
  {
    using seconds_f = std::chrono::duration<float>;
    const auto frame_context = RequireFrameContext(state);
    return std::chrono::duration_cast<seconds_f>(
      frame_context->GetGameDeltaTime().get())
      .count();
  }

  auto LuaTimeDeltaSeconds(lua_State* state) -> int
  {
    lua_pushnumber(state, ContextDeltaSeconds(state));
    return 1;
  }

  auto LuaTimeFixedDeltaSeconds(lua_State* state) -> int
  {
    using seconds_f = std::chrono::duration<float>;
    const auto frame_context = RequireFrameContext(state);

    lua_pushnumber(state,
      std::chrono::duration_cast<seconds_f>(
        frame_context->GetFixedDeltaTime().get())
        .count());
    return 1;
  }

  auto LuaTimeFrameSequenceNumber(lua_State* state) -> int
  {
    const auto frame_context = RequireFrameContext(state);
    lua_pushinteger(state,
      static_cast<lua_Integer>(frame_context->GetFrameSequenceNumber().get()));
    return 1;
  }

  auto LuaTimeScale(lua_State* state) -> int
  {
    const auto frame_context = RequireFrameContext(state);
    lua_pushnumber(state, frame_context->GetTimeScale());
    return 1;
  }

  auto LuaTimePaused(lua_State* state) -> int
  {
    const auto frame_context = RequireFrameContext(state);
    lua_pushboolean(state, static_cast<int>(frame_context->IsGamePaused()));
    return 1;
  }

  auto LuaTimeInterpolationAlpha(lua_State* state) -> int
  {
    const auto frame_context = RequireFrameContext(state);
    lua_pushnumber(state, frame_context->GetInterpolationAlpha());
    return 1;
  }

  auto LuaTimeCurrentFPS(lua_State* state) -> int
  {
    const auto frame_context = RequireFrameContext(state);
    lua_pushnumber(state, frame_context->GetCurrentFPS());
    return 1;
  }
} // namespace

auto RegisterTimeBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "time");

  lua_pushcfunction(state, LuaTimeDeltaSeconds, "time.delta_seconds");
  lua_setfield(state, module_index, "delta_seconds");

  lua_pushcfunction(
    state, LuaTimeFixedDeltaSeconds, "time.fixed_delta_seconds");
  lua_setfield(state, module_index, "fixed_delta_seconds");

  lua_pushcfunction(
    state, LuaTimeFrameSequenceNumber, "time.frame_sequence_number");
  lua_setfield(state, module_index, "frame_sequence_number");

  lua_pushcfunction(state, LuaTimeScale, "time.time_scale");
  lua_setfield(state, module_index, "time_scale");

  lua_pushcfunction(state, LuaTimePaused, "time.is_paused");
  lua_setfield(state, module_index, "is_paused");

  lua_pushcfunction(
    state, LuaTimeInterpolationAlpha, "time.interpolation_alpha");
  lua_setfield(state, module_index, "interpolation_alpha");

  lua_pushcfunction(state, LuaTimeCurrentFPS, "time.current_fps");
  lua_setfield(state, module_index, "current_fps");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
