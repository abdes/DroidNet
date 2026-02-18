//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ConventionsBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto PushVec3(lua_State* state, const Vec3& value) -> void
  {
    lua_newtable(state);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z);
    lua_setfield(state, -2, "z");
  }
} // namespace

auto RegisterConventionsBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int conventions_index
    = PushOxygenSubtable(state, oxygen_table_index, "conventions");

  lua_pushliteral(state, "right_handed");
  lua_setfield(state, conventions_index, "handedness");

  lua_newtable(state); // world
  PushVec3(state, space::move::Up);
  lua_setfield(state, -2, "up");
  PushVec3(state, space::move::Forward);
  lua_setfield(state, -2, "forward");
  PushVec3(state, space::move::Right);
  lua_setfield(state, -2, "right");
  lua_setfield(state, conventions_index, "world");

  lua_newtable(state); // view
  PushVec3(state, space::look::Up);
  lua_setfield(state, -2, "up");
  PushVec3(state, space::look::Forward);
  lua_setfield(state, -2, "forward");
  PushVec3(state, space::look::Right);
  lua_setfield(state, -2, "right");
  lua_setfield(state, conventions_index, "view");

  lua_newtable(state); // clip
  lua_pushnumber(state, space::clip::ZNear);
  lua_setfield(state, -2, "z_near");
  lua_pushnumber(state, space::clip::ZFar);
  lua_setfield(state, -2, "z_far");
  lua_pushboolean(state, space::clip::FrontFaceCCW ? 1 : 0);
  lua_setfield(state, -2, "front_face_ccw");
  lua_setfield(state, conventions_index, "clip");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
