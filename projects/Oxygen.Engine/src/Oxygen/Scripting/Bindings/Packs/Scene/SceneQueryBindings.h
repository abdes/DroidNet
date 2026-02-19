#pragma once

#include <string>

#include <lua.h>

#include <Oxygen/Scene/SceneQuery.h>

namespace oxygen::scripting::bindings {

auto RegisterSceneQueryMetatable(lua_State* state) -> void;
auto PushSceneQuery(
  lua_State* state, scene::SceneQuery query, std::string pattern) -> int;

} // namespace oxygen::scripting::bindings
