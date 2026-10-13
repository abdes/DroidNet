#pragma once

#include <span>

#include <lua.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>

namespace oxygen::scripting::bindings {

auto RegisterSceneEnvironmentMetatables(lua_State* state) -> void;
auto DescribeSceneEnvironmentBindings()
  -> std::span<const BindingNamespaceDescription>;
auto PushSceneEnvironment(
  lua_State* state, observer_ptr<scene::Scene> scene_ref) -> int;

} // namespace oxygen::scripting::bindings
