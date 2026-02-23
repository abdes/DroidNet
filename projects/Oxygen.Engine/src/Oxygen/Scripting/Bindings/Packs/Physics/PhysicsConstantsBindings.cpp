//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsConstantsBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto PushReadOnlyStringEnumTable(lua_State* state,
    const std::initializer_list<std::pair<const char*, const char*>>& values)
    -> int
  {
    lua_createtable(state, 0, static_cast<int>(values.size()));
    for (const auto& [key, value] : values) {
      lua_pushstring(state, value);
      lua_setfield(state, -2, key);
    }
    lua_setreadonly(state, -1, 1);
    return 1;
  }

} // namespace

auto RegisterPhysicsConstantsBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto constants_index
    = PushOxygenSubtable(state, physics_index, "constants");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "static", "static" },
      { "dynamic", "dynamic" },
      { "kinematic", "kinematic" },
    }));
  lua_setfield(state, constants_index, "body_type");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "none", "none" },
      { "enable_gravity", "enable_gravity" },
      { "is_trigger", "is_trigger" },
      { "enable_ccd", "enable_ccd" },
    }));
  lua_setfield(state, constants_index, "body_flags");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "contact_begin", "contact_begin" },
      { "contact_end", "contact_end" },
      { "trigger_begin", "trigger_begin" },
      { "trigger_end", "trigger_end" },
    }));
  lua_setfield(state, constants_index, "event_type");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "fixed", "fixed" },
      { "distance", "distance" },
      { "hinge", "hinge" },
      { "slider", "slider" },
      { "spherical", "spherical" },
    }));
  lua_setfield(state, constants_index, "joint_type");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "simulation", "simulation" },
      { "command", "command" },
    }));
  lua_setfield(state, constants_index, "aggregate_authority");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "none", "none" },
      { "euclidean", "euclidean" },
      { "geodesic", "geodesic" },
    }));
  lua_setfield(state, constants_index, "soft_body_tether_mode");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "sphere", "sphere" },
      { "capsule", "capsule" },
      { "box", "box" },
      { "cylinder", "cylinder" },
      { "cone", "cone" },
      { "convex_hull", "convex_hull" },
      { "triangle_mesh", "triangle_mesh" },
      { "height_field", "height_field" },
      { "plane", "plane" },
      { "world_boundary", "world_boundary" },
      { "compound", "compound" },
    }));
  lua_setfield(state, constants_index, "shape_type");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "convex", "convex" },
      { "mesh", "mesh" },
      { "height_field", "height_field" },
      { "compound", "compound" },
    }));
  lua_setfield(state, constants_index, "shape_payload_type");

  static_cast<void>(PushReadOnlyStringEnumTable(state,
    {
      { "aabb_clamp", "aabb_clamp" },
      { "plane_set", "plane_set" },
    }));
  lua_setfield(state, constants_index, "world_boundary_mode");

  lua_setreadonly(state, constants_index, 1);
  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
