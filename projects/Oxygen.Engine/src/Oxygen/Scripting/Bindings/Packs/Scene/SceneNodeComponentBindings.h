#pragma once

#include <span>

#include <lua.h>

#include <Oxygen/Scripting/Bindings/BindingTable.h>

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

auto DescribeSceneNodeCameraBindings()
  -> std::span<const BindingNamespaceDescription>;
auto DescribeSceneNodeLightBindings()
  -> std::span<const BindingNamespaceDescription>;
auto DescribeSceneNodeRenderableBindings()
  -> std::span<const BindingNamespaceDescription>;
auto DescribeSceneNodeScriptingBindings()
  -> std::span<const BindingNamespaceDescription>;

} // namespace oxygen::scripting::bindings
