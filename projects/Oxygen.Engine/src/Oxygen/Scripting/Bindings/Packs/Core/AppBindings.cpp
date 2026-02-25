//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Engine/IAsyncEngine.h>
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
} // namespace

auto RegisterAppBindings(lua_State* state, const int oxygen_table_index) -> void
{
  const int module_index = PushOxygenSubtable(state, oxygen_table_index, "app");

  lua_pushcfunction(state, LuaAppName, "app.name");
  lua_setfield(state, module_index, "name");

  lua_pushcfunction(state, LuaAppVersion, "app.version");
  lua_setfield(state, module_index, "version");

  lua_pushcfunction(state, LuaAppTargetFps, "app.target_fps");
  lua_setfield(state, module_index, "target_fps");

  lua_pushcfunction(state, LuaAppFrameCountLimit, "app.frame_count_limit");
  lua_setfield(state, module_index, "frame_count_limit");

  lua_pushcfunction(
    state, LuaAppAssetLoaderEnabled, "app.asset_loader_enabled");
  lua_setfield(state, module_index, "asset_loader_enabled");

  lua_pushcfunction(state, LuaAppMaxTargetFps, "app.max_target_fps");
  lua_setfield(state, module_index, "max_target_fps");

  lua_pushcfunction(state, LuaAppIsRunning, "app.is_running");
  lua_setfield(state, module_index, "is_running");

  lua_pushcfunction(state, LuaAppRequestStop, "app.request_stop");
  lua_setfield(state, module_index, "request_stop");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
