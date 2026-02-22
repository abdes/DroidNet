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

  lua_setreadonly(state, constants_index, 1);
  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
