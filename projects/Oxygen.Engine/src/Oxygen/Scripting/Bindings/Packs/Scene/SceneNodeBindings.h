#pragma once

#include <lua.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::scripting::bindings {

auto RegisterSceneNodeMetatable(lua_State* state) -> void;

auto TryCheckSceneNode(lua_State* state, int index) -> scene::SceneNode*;
auto PushSceneNode(lua_State* state, scene::SceneNode node) -> int;
auto GetScene(lua_State* state) -> observer_ptr<scene::Scene>;

auto TryCheckVec3(lua_State* state, int index, Vec3& out) -> bool;
auto PushVec3(lua_State* state, const Vec3& v) -> int;

struct QuatUserdata {
  float x, y, z, w;
};

auto TryCheckQuat(lua_State* state, int index) -> QuatUserdata*;
auto PushQuat(lua_State* state, float x, float y, float z, float w) -> int;

} // namespace oxygen::scripting::bindings
