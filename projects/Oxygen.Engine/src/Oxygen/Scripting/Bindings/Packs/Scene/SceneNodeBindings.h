#pragma once

#include <span>

#include <lua.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/MathBindings.h>

namespace oxygen::scripting::bindings {

//! Registry name of the `SceneNode` metatable.
inline constexpr const char* kSceneNodeMetatable = "oxygen.scene.node";

auto RegisterSceneNodeMetatable(lua_State* state) -> void;
auto DescribeSceneNodeBindings()
  -> std::span<const BindingNamespaceDescription>;

auto TryCheckSceneNode(lua_State* state, int index) -> scene::SceneNode*;
auto PushSceneNode(lua_State* state, scene::SceneNode node) -> int;
auto GetScene(lua_State* state) -> observer_ptr<scene::Scene>;

auto TryCheckVec3(lua_State* state, int index, Vec3& out) -> bool;
auto PushVec3(lua_State* state, const Vec3& v) -> int;

} // namespace oxygen::scripting::bindings
