//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <span>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/TimeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto RequireFrameContext(lua_State* state)
    -> observer_ptr<engine::FrameContext>
  {
    const auto frame_context = GetActiveFrameContext(state);
    if (frame_context == nullptr) {
      (void)luaL_error(state, "oxygen.time requires active FrameContext");
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

  constexpr auto kTimeFunctions = std::to_array<BindingFunction>({
    {
      .name = "delta_seconds",
      .function = LuaTimeDeltaSeconds,
      .signature = "() -> number",
    },
    {
      .name = "fixed_delta_seconds",
      .function = LuaTimeFixedDeltaSeconds,
      .signature = "() -> number",
    },
    {
      .name = "frame_sequence_number",
      .function = LuaTimeFrameSequenceNumber,
      .signature = "() -> number",
    },
    {
      .name = "time_scale",
      .function = LuaTimeScale,
      .signature = "() -> number",
    },
    {
      .name = "is_paused",
      .function = LuaTimePaused,
      .signature = "() -> boolean",
    },
    {
      .name = "interpolation_alpha",
      .function = LuaTimeInterpolationAlpha,
      .signature = "() -> number",
    },
    {
      .name = "current_fps",
      .function = LuaTimeCurrentFPS,
      .signature = "() -> number",
    },
  });

  constexpr auto kTimeDescription = std::to_array<BindingNamespaceDescription>({
    { .path = "time", .functions = kTimeFunctions },
  });
} // namespace

auto RegisterTimeBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "time");
  RegisterFunctions(state, module_index, kTimeFunctions);
  lua_pop(state, 1);
}

auto DescribeTimeBindings() -> std::span<const BindingNamespaceDescription>
{
  return kTimeDescription;
}

} // namespace oxygen::scripting::bindings
