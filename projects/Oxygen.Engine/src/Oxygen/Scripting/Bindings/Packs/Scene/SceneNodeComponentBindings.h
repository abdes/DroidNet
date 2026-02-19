#pragma once

#include <lua.h>

namespace oxygen::scripting::bindings {

auto RegisterSceneNodeCameraMethods(lua_State* state, int metatable_index)
  -> void;
auto RegisterSceneNodeLightMethods(lua_State* state, int metatable_index)
  -> void;
auto RegisterSceneNodeRenderableMethods(lua_State* state, int metatable_index)
  -> void;
auto RegisterSceneNodeScriptingMethods(lua_State* state, int metatable_index)
  -> void;

auto RegisterSceneNodeComponentMethods(lua_State* state, int metatable_index)
  -> void;

} // namespace oxygen::scripting::bindings
