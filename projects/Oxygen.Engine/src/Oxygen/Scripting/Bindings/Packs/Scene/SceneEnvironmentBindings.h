#pragma once

#include <lua.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::scripting::bindings {

auto RegisterSceneEnvironmentMetatables(lua_State* state) -> void;
auto PushSceneEnvironment(
  lua_State* state, observer_ptr<scene::Scene> scene_ref) -> int;

} // namespace oxygen::scripting::bindings
