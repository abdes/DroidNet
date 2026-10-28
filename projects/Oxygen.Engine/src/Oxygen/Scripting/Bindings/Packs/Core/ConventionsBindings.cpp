//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <span>

#include <lua.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ConventionsBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto PushVec3(lua_State* state, const Vec3& value) -> void
  {
    lua_pushvector(state, value.x, value.y, value.z);
  }

  constexpr auto kConventionsFields = std::to_array<BindingField>({
    { .name = "handedness", .type = "string" },
    { .name = "world", .type = "Axes" },
    { .name = "node", .type = "Axes" },
    { .name = "view", .type = "Axes" },
    { .name = "clip", .type = "ClipConventions" },
  });

  constexpr auto kConventionsTypes = std::to_array<BindingTypeAlias>({
    {
      .name = "Axes",
      .definition = "{ up: vector, forward: vector, right: vector }",
    },
    {
      .name = "ClipConventions",
      .definition
      = "{ z_near: number, z_far: number, front_face_ccw: boolean }",
    },
  });

  constexpr auto kConventionsDescription
    = std::to_array<BindingNamespaceDescription>({
      {
        .path = "conventions",
        .fields = kConventionsFields,
        .types = kConventionsTypes,
      },
    });
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

  lua_newtable(state); // node: the local frame of every node, cameras included
  PushVec3(state, space::node::Up);
  lua_setfield(state, -2, "up");
  PushVec3(state, space::node::Forward);
  lua_setfield(state, -2, "forward");
  PushVec3(state, space::node::Right);
  lua_setfield(state, -2, "right");
  lua_setfield(state, conventions_index, "node");

  lua_newtable(state); // view space, which describes no node
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

auto DescribeConventionsBindings()
  -> std::span<const BindingNamespaceDescription>
{
  return kConventionsDescription;
}

} // namespace oxygen::scripting::bindings
