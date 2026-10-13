//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <span>

#include <lua.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/AppBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto RequireEngine(lua_State* state) -> observer_ptr<IAsyncEngine>
  {
    const auto engine = GetActiveEngine(state);
    CHECK_NOTNULL_F(engine);
    return engine;
  }

  auto LuaAppName(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    const auto& name = engine->GetEngineConfig().application.name;
    lua_pushlstring(state, name.data(), name.size());
    return 1;
  }

  auto LuaAppVersion(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    lua_pushinteger(state,
      static_cast<lua_Integer>(engine->GetEngineConfig().application.version));
    return 1;
  }

  auto LuaAppTargetFps(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    lua_pushinteger(
      state, static_cast<lua_Integer>(engine->GetEngineConfig().target_fps));
    return 1;
  }

  auto LuaAppFrameCountLimit(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    lua_pushinteger(
      state, static_cast<lua_Integer>(engine->GetEngineConfig().frame_count));
    return 1;
  }

  auto LuaAppAssetLoaderEnabled(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    lua_pushboolean(
      state, static_cast<int>(engine->GetEngineConfig().enable_asset_loader));
    return 1;
  }

  auto LuaAppMaxTargetFps(lua_State* state) -> int
  {
    lua_pushinteger(
      state, static_cast<lua_Integer>(EngineConfig::kMaxTargetFps));
    return 1;
  }

  auto LuaAppIsRunning(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    lua_pushboolean(state, static_cast<int>(engine->IsRunning()));
    return 1;
  }

  auto LuaAppRequestStop(lua_State* state) -> int
  {
    const auto engine = RequireEngine(state);
    engine->Stop();
    return 0;
  }

  constexpr auto kAppFunctions = std::to_array<BindingFunction>({
    { .name = "name", .function = LuaAppName, .signature = "() -> string" },
    {
      .name = "version",
      .function = LuaAppVersion,
      .signature = "() -> number",
    },
    {
      .name = "target_fps",
      .function = LuaAppTargetFps,
      .signature = "() -> number",
    },
    {
      .name = "frame_count_limit",
      .function = LuaAppFrameCountLimit,
      .signature = "() -> number",
    },
    {
      .name = "asset_loader_enabled",
      .function = LuaAppAssetLoaderEnabled,
      .signature = "() -> boolean",
    },
    {
      .name = "max_target_fps",
      .function = LuaAppMaxTargetFps,
      .signature = "() -> number",
    },
    {
      .name = "is_running",
      .function = LuaAppIsRunning,
      .signature = "() -> boolean",
    },
    {
      .name = "request_stop",
      .function = LuaAppRequestStop,
      .signature = "() -> ()",
    },
  });

  constexpr auto kAppDescription = std::to_array<BindingNamespaceDescription>({
    { .path = "app", .functions = kAppFunctions },
  });
} // namespace

auto RegisterAppBindings(lua_State* state, const int oxygen_table_index) -> void
{
  const int module_index = PushOxygenSubtable(state, oxygen_table_index, "app");
  RegisterFunctions(state, module_index, kAppFunctions);
  lua_pop(state, 1);
}

auto DescribeAppBindings() -> std::span<const BindingNamespaceDescription>
{
  return kAppDescription;
}

} // namespace oxygen::scripting::bindings
