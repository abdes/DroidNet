//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto CheckFinite(lua_State* state, const float value, const char* field_name)
    -> float
  {
    if (!std::isfinite(value)) {
      luaL_error(state, "physics descriptor field '%s' must be finite",
        field_name == nullptr ? "<unknown>" : field_name);
      return 0.0F;
    }
    return value;
  }

  auto ReadOptionalFloatField(lua_State* state, const int table_index,
    const char* field_name, const float default_value) -> float
  {
    lua_getfield(state, table_index, field_name);
    const auto value = lua_isnil(state, -1) == 0
      ? static_cast<float>(luaL_checknumber(state, -1))
      : default_value;
    lua_pop(state, 1);
    return CheckFinite(state, value, field_name);
  }

  auto ParseWorldBoundaryMode(lua_State* state, const int table_index)
    -> physics::WorldBoundaryMode
  {
    lua_getfield(state, table_index, "boundary_mode");
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      lua_getfield(state, table_index, "mode");
    }
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      luaL_error(
        state, "world_boundary shape requires 'boundary_mode' (or 'mode')");
      return physics::WorldBoundaryMode::kInvalid;
    }

    physics::WorldBoundaryMode mode = physics::WorldBoundaryMode::kInvalid;
    if (lua_isnumber(state, -1) != 0) {
      const auto raw = luaL_checkinteger(state, -1);
      if (raw
        == static_cast<lua_Integer>(physics::WorldBoundaryMode::kAabbClamp)) {
        mode = physics::WorldBoundaryMode::kAabbClamp;
      } else if (raw
        == static_cast<lua_Integer>(physics::WorldBoundaryMode::kPlaneSet)) {
        mode = physics::WorldBoundaryMode::kPlaneSet;
      }
    } else {
      const auto* mode_text = luaL_checkstring(state, -1);
      const auto mode_view
        = std::string_view(mode_text == nullptr ? "" : mode_text);
      if (mode_view == "aabb_clamp") {
        mode = physics::WorldBoundaryMode::kAabbClamp;
      } else if (mode_view == "plane_set") {
        mode = physics::WorldBoundaryMode::kPlaneSet;
      }
    }
    lua_pop(state, 1);
    if (mode == physics::WorldBoundaryMode::kInvalid) {
      luaL_error(state,
        "world_boundary boundary_mode must be 'aabb_clamp' or 'plane_set'");
      return physics::WorldBoundaryMode::kInvalid;
    }
    return mode;
  }

  auto ParseShapePayloadType(lua_State* state, const int payload_table_index)
    -> physics::ShapePayloadType
  {
    lua_getfield(state, payload_table_index, "payload_type");
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      lua_getfield(state, payload_table_index, "type");
    }
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      luaL_error(
        state, "shape.cooked_payload requires 'payload_type' (or 'type')");
      return physics::ShapePayloadType::kInvalid;
    }

    physics::ShapePayloadType payload_type
      = physics::ShapePayloadType::kInvalid;
    if (lua_isnumber(state, -1) != 0) {
      const auto raw = luaL_checkinteger(state, -1);
      if (raw == static_cast<lua_Integer>(physics::ShapePayloadType::kConvex)) {
        payload_type = physics::ShapePayloadType::kConvex;
      } else if (raw
        == static_cast<lua_Integer>(physics::ShapePayloadType::kMesh)) {
        payload_type = physics::ShapePayloadType::kMesh;
      } else if (raw
        == static_cast<lua_Integer>(physics::ShapePayloadType::kHeightField)) {
        payload_type = physics::ShapePayloadType::kHeightField;
      } else if (raw
        == static_cast<lua_Integer>(physics::ShapePayloadType::kCompound)) {
        payload_type = physics::ShapePayloadType::kCompound;
      }
    } else {
      const auto* payload_type_text = luaL_checkstring(state, -1);
      const auto payload_type_view = std::string_view(
        payload_type_text == nullptr ? "" : payload_type_text);
      if (payload_type_view == "convex") {
        payload_type = physics::ShapePayloadType::kConvex;
      } else if (payload_type_view == "mesh") {
        payload_type = physics::ShapePayloadType::kMesh;
      } else if (payload_type_view == "height_field") {
        payload_type = physics::ShapePayloadType::kHeightField;
      } else if (payload_type_view == "compound") {
        payload_type = physics::ShapePayloadType::kCompound;
      }
    }
    lua_pop(state, 1);
    if (payload_type == physics::ShapePayloadType::kInvalid) {
      luaL_error(state,
        "shape.cooked_payload.payload_type must be one of 'convex', 'mesh', "
        "'height_field', or 'compound'");
      return physics::ShapePayloadType::kInvalid;
    }
    return payload_type;
  }

  auto ParseHexNybble(lua_State* state, const char c, const char* field_name)
    -> uint8_t
  {
    if (c >= '0' && c <= '9') {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint8_t>(10 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F') {
      return static_cast<uint8_t>(10 + (c - 'A'));
    }
    luaL_error(state, "%s contains non-hex character", field_name);
    return 0;
  }

  auto ParseHexPayloadData(lua_State* state, const std::string_view hex_text,
    const char* field_name) -> std::vector<uint8_t>
  {
    if ((hex_text.size() % 2U) != 0U) {
      luaL_error(state, "%s must contain an even number of hex characters",
        field_name == nullptr ? "shape.cooked_payload.data_hex" : field_name);
      return {};
    }

    std::vector<uint8_t> bytes {};
    bytes.reserve(hex_text.size() / 2U);
    for (size_t i = 0; i < hex_text.size(); i += 2U) {
      const auto hi = ParseHexNybble(state, hex_text[i], field_name);
      const auto lo = ParseHexNybble(state, hex_text[i + 1U], field_name);
      bytes.push_back(static_cast<uint8_t>((hi << 4U) | lo));
    }
    return bytes;
  }

  auto ParseCookedPayload(lua_State* state, const int shape_table_index,
    const physics::ShapePayloadType expected_payload_type)
    -> physics::CookedShapePayload
  {
    lua_getfield(state, shape_table_index, "cooked_payload");
    const auto payload_table_index = lua_absindex(state, -1);
    luaL_checktype(state, payload_table_index, LUA_TTABLE);

    const auto payload_type = ParseShapePayloadType(state, payload_table_index);
    if (payload_type != expected_payload_type) {
      lua_pop(state, 1);
      luaL_error(state,
        "shape.cooked_payload.payload_type does not match this shape type");
      return {};
    }

    std::vector<uint8_t> payload_data {};

    lua_getfield(state, payload_table_index, "data");
    if (lua_isnil(state, -1) == 0) {
      size_t payload_len = 0;
      const auto* payload_ptr = luaL_checklstring(state, -1, &payload_len);
      payload_data.assign(payload_ptr, payload_ptr + payload_len);
      lua_pop(state, 1);
    } else {
      lua_pop(state, 1);
      lua_getfield(state, payload_table_index, "data_hex");
      const auto* hex_text = luaL_checkstring(state, -1);
      const auto hex_view
        = std::string_view(hex_text == nullptr ? "" : hex_text);
      payload_data
        = ParseHexPayloadData(state, hex_view, "shape.cooked_payload.data_hex");
      lua_pop(state, 1);
    }

    lua_pop(state, 1);

    if (payload_data.empty()) {
      luaL_error(state, "shape.cooked_payload data must be non-empty");
      return {};
    }

    return physics::CookedShapePayload {
      .payload_type = payload_type,
      .data = std::move(payload_data),
    };
  }

  auto SetMetatable(lua_State* state, const char* metatable_name) -> void
  {
    if (luaL_getmetatable(state, metatable_name) == 0) {
      lua_pop(state, 1);
      luaL_error(state, "missing metatable '%s'", metatable_name);
      return;
    }
    lua_setmetatable(state, -2);
  }

  auto BodyHandleIsValid(lua_State* state) -> int
  {
    const auto* handle = CheckBodyHandle(state, 1);
    lua_pushboolean(state, physics::IsValid(handle->body_id) ? 1 : 0);
    return 1;
  }

  auto BodyHandleToString(lua_State* state) -> int
  {
    const auto* handle = CheckBodyHandle(state, 1);
    const auto text = std::string("BodyHandle{world=")
                        .append(physics::to_string(handle->world_id))
                        .append(", body=")
                        .append(physics::to_string(handle->body_id))
                        .append(", type=")
                        .append(physics::body::to_string(handle->body_type))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto BodyHandleEq(lua_State* state) -> int
  {
    const auto* lhs = CheckBodyHandle(state, 1);
    const auto* rhs = CheckBodyHandle(state, 2);
    const auto equal
      = lhs->world_id == rhs->world_id && lhs->body_id == rhs->body_id;
    lua_pushboolean(state, equal ? 1 : 0);
    return 1;
  }

  auto CharacterHandleIsValid(lua_State* state) -> int
  {
    const auto* handle = CheckCharacterHandle(state, 1);
    lua_pushboolean(state, physics::IsValid(handle->character_id) ? 1 : 0);
    return 1;
  }

  auto CharacterHandleToString(lua_State* state) -> int
  {
    const auto* handle = CheckCharacterHandle(state, 1);
    const auto text = std::string("CharacterHandle{world=")
                        .append(physics::to_string(handle->world_id))
                        .append(", character=")
                        .append(physics::to_string(handle->character_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto CharacterHandleEq(lua_State* state) -> int
  {
    const auto* lhs = CheckCharacterHandle(state, 1);
    const auto* rhs = CheckCharacterHandle(state, 2);
    const auto equal = lhs->world_id == rhs->world_id
      && lhs->character_id == rhs->character_id;
    lua_pushboolean(state, equal ? 1 : 0);
    return 1;
  }

  auto BodyIdIsValid(lua_State* state) -> int
  {
    const auto* id = CheckBodyId(state, 1);
    lua_pushboolean(state, physics::IsValid(id->body_id) ? 1 : 0);
    return 1;
  }

  auto BodyIdToString(lua_State* state) -> int
  {
    const auto* id = CheckBodyId(state, 1);
    const auto text = std::string("BodyId{")
                        .append(physics::to_string(id->body_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto BodyIdEq(lua_State* state) -> int
  {
    const auto* lhs = CheckBodyId(state, 1);
    const auto* rhs = CheckBodyId(state, 2);
    lua_pushboolean(state, lhs->body_id == rhs->body_id ? 1 : 0);
    return 1;
  }

  auto CharacterIdIsValid(lua_State* state) -> int
  {
    const auto* id = CheckCharacterId(state, 1);
    lua_pushboolean(state, physics::IsValid(id->character_id) ? 1 : 0);
    return 1;
  }

  auto CharacterIdToString(lua_State* state) -> int
  {
    const auto* id = CheckCharacterId(state, 1);
    const auto text = std::string("CharacterId{")
                        .append(physics::to_string(id->character_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto CharacterIdEq(lua_State* state) -> int
  {
    const auto* lhs = CheckCharacterId(state, 1);
    const auto* rhs = CheckCharacterId(state, 2);
    lua_pushboolean(state, lhs->character_id == rhs->character_id ? 1 : 0);
    return 1;
  }

  auto AggregateHandleIsValid(lua_State* state) -> int
  {
    const auto* handle = CheckAggregateHandle(state, 1);
    lua_pushboolean(state, physics::IsValid(handle->aggregate_id) ? 1 : 0);
    return 1;
  }

  auto AggregateHandleToString(lua_State* state) -> int
  {
    const auto* handle = CheckAggregateHandle(state, 1);
    const auto text = std::string("AggregateHandle{world=")
                        .append(physics::to_string(handle->world_id))
                        .append(", aggregate=")
                        .append(physics::to_string(handle->aggregate_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto AggregateHandleEq(lua_State* state) -> int
  {
    const auto* lhs = CheckAggregateHandle(state, 1);
    const auto* rhs = CheckAggregateHandle(state, 2);
    const auto equal = lhs->world_id == rhs->world_id
      && lhs->aggregate_id == rhs->aggregate_id;
    lua_pushboolean(state, equal ? 1 : 0);
    return 1;
  }

  auto AggregateIdIsValid(lua_State* state) -> int
  {
    const auto* id = CheckAggregateId(state, 1);
    lua_pushboolean(state, physics::IsValid(id->aggregate_id) ? 1 : 0);
    return 1;
  }

  auto AggregateIdToString(lua_State* state) -> int
  {
    const auto* id = CheckAggregateId(state, 1);
    const auto text = std::string("AggregateId{")
                        .append(physics::to_string(id->aggregate_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto AggregateIdEq(lua_State* state) -> int
  {
    const auto* lhs = CheckAggregateId(state, 1);
    const auto* rhs = CheckAggregateId(state, 2);
    lua_pushboolean(state, lhs->aggregate_id == rhs->aggregate_id ? 1 : 0);
    return 1;
  }

  auto JointIdIsValid(lua_State* state) -> int
  {
    const auto* id = CheckJointId(state, 1);
    lua_pushboolean(state, physics::IsValid(id->joint_id) ? 1 : 0);
    return 1;
  }

  auto JointIdToString(lua_State* state) -> int
  {
    const auto* id = CheckJointId(state, 1);
    const auto text = std::string("JointId{")
                        .append(physics::to_string(id->joint_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto JointIdEq(lua_State* state) -> int
  {
    const auto* lhs = CheckJointId(state, 1);
    const auto* rhs = CheckJointId(state, 2);
    lua_pushboolean(state, lhs->joint_id == rhs->joint_id ? 1 : 0);
    return 1;
  }

  auto JointHandleIsValid(lua_State* state) -> int
  {
    const auto* handle = CheckJointHandle(state, 1);
    lua_pushboolean(state, physics::IsValid(handle->joint_id) ? 1 : 0);
    return 1;
  }

  auto JointHandleToString(lua_State* state) -> int
  {
    const auto* handle = CheckJointHandle(state, 1);
    const auto text = std::string("JointHandle{world=")
                        .append(physics::to_string(handle->world_id))
                        .append(", joint=")
                        .append(physics::to_string(handle->joint_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto JointHandleEq(lua_State* state) -> int
  {
    const auto* lhs = CheckJointHandle(state, 1);
    const auto* rhs = CheckJointHandle(state, 2);
    const auto equal
      = lhs->world_id == rhs->world_id && lhs->joint_id == rhs->joint_id;
    lua_pushboolean(state, equal ? 1 : 0);
    return 1;
  }

  auto ShapeIdIsValid(lua_State* state) -> int
  {
    const auto* id = CheckShapeId(state, 1);
    lua_pushboolean(state, physics::IsValid(id->shape_id) ? 1 : 0);
    return 1;
  }

  auto ShapeIdToString(lua_State* state) -> int
  {
    const auto* id = CheckShapeId(state, 1);
    const auto text = std::string("ShapeId{")
                        .append(physics::to_string(id->shape_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto ShapeIdEq(lua_State* state) -> int
  {
    const auto* lhs = CheckShapeId(state, 1);
    const auto* rhs = CheckShapeId(state, 2);
    lua_pushboolean(state, lhs->shape_id == rhs->shape_id ? 1 : 0);
    return 1;
  }

  auto ShapeInstanceIdIsValid(lua_State* state) -> int
  {
    const auto* id = CheckShapeInstanceId(state, 1);
    lua_pushboolean(state, physics::IsValid(id->shape_instance_id) ? 1 : 0);
    return 1;
  }

  auto ShapeInstanceIdToString(lua_State* state) -> int
  {
    const auto* id = CheckShapeInstanceId(state, 1);
    const auto text = std::string("ShapeInstanceId{")
                        .append(physics::to_string(id->shape_instance_id))
                        .append("}");
    lua_pushstring(state, text.c_str());
    return 1;
  }

  auto ShapeInstanceIdEq(lua_State* state) -> int
  {
    const auto* lhs = CheckShapeInstanceId(state, 1);
    const auto* rhs = CheckShapeInstanceId(state, 2);
    lua_pushboolean(
      state, lhs->shape_instance_id == rhs->shape_instance_id ? 1 : 0);
    return 1;
  }

  auto RegisterMetatableWithCommonMethods(lua_State* state,
    const char* metatable_name, lua_CFunction is_valid_fn, lua_CFunction eq_fn,
    lua_CFunction tostring_fn) -> void
  {
    static_cast<void>(luaL_newmetatable(state, metatable_name));

    lua_pushcfunction(state, is_valid_fn, "is_valid");
    lua_setfield(state, -2, "is_valid");
    lua_pushcfunction(state, tostring_fn, "to_string");
    lua_setfield(state, -2, "to_string");

    lua_pushcfunction(state, tostring_fn, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, eq_fn, "__eq");
    lua_setfield(state, -2, "__eq");

    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");
    lua_pop(state, 1);
  }

} // namespace

auto RegisterPhysicsBodyHandleMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsBodyHandleMetatable,
    BodyHandleIsValid, BodyHandleEq, BodyHandleToString);
}

auto RegisterPhysicsCharacterHandleMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsCharacterHandleMetatable,
    CharacterHandleIsValid, CharacterHandleEq, CharacterHandleToString);
}

auto RegisterPhysicsBodyIdMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(
    state, kPhysicsBodyIdMetatable, BodyIdIsValid, BodyIdEq, BodyIdToString);
}

auto RegisterPhysicsCharacterIdMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsCharacterIdMetatable,
    CharacterIdIsValid, CharacterIdEq, CharacterIdToString);
}

auto RegisterPhysicsAggregateHandleMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsAggregateHandleMetatable,
    AggregateHandleIsValid, AggregateHandleEq, AggregateHandleToString);
}

auto RegisterPhysicsAggregateIdMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsAggregateIdMetatable,
    AggregateIdIsValid, AggregateIdEq, AggregateIdToString);
}

auto RegisterPhysicsJointHandleMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsJointHandleMetatable,
    JointHandleIsValid, JointHandleEq, JointHandleToString);
}

auto RegisterPhysicsJointIdMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsJointIdMetatable,
    JointIdIsValid, JointIdEq, JointIdToString);
}

auto RegisterPhysicsShapeIdMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsShapeIdMetatable,
    ShapeIdIsValid, ShapeIdEq, ShapeIdToString);
}

auto RegisterPhysicsShapeInstanceIdMetatable(lua_State* state) -> void
{
  RegisterMetatableWithCommonMethods(state, kPhysicsShapeInstanceIdMetatable,
    ShapeInstanceIdIsValid, ShapeInstanceIdEq, ShapeInstanceIdToString);
}

auto PushBodyHandle(lua_State* state, const physics::WorldId world_id,
  const physics::BodyId body_id, const physics::body::BodyType body_type) -> int
{
  auto* userdata = static_cast<PhysicsBodyHandleUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsBodyHandleUserdata)));
  *userdata = PhysicsBodyHandleUserdata {
    .world_id = world_id,
    .body_id = body_id,
    .body_type = body_type,
  };
  SetMetatable(state, kPhysicsBodyHandleMetatable);
  return 1;
}

auto CheckBodyHandle(lua_State* state, const int index)
  -> PhysicsBodyHandleUserdata*
{
  return static_cast<PhysicsBodyHandleUserdata*>(
    luaL_checkudata(state, index, kPhysicsBodyHandleMetatable));
}

auto PushCharacterHandle(lua_State* state, const physics::WorldId world_id,
  const physics::CharacterId character_id) -> int
{
  auto* userdata = static_cast<PhysicsCharacterHandleUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsCharacterHandleUserdata)));
  *userdata = PhysicsCharacterHandleUserdata {
    .world_id = world_id,
    .character_id = character_id,
  };
  SetMetatable(state, kPhysicsCharacterHandleMetatable);
  return 1;
}

auto CheckCharacterHandle(lua_State* state, const int index)
  -> PhysicsCharacterHandleUserdata*
{
  return static_cast<PhysicsCharacterHandleUserdata*>(
    luaL_checkudata(state, index, kPhysicsCharacterHandleMetatable));
}

auto PushBodyId(lua_State* state, const physics::BodyId body_id) -> int
{
  auto* userdata = static_cast<PhysicsBodyIdUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsBodyIdUserdata)));
  userdata->body_id = body_id;
  SetMetatable(state, kPhysicsBodyIdMetatable);
  return 1;
}

auto CheckBodyId(lua_State* state, const int index) -> PhysicsBodyIdUserdata*
{
  return static_cast<PhysicsBodyIdUserdata*>(
    luaL_checkudata(state, index, kPhysicsBodyIdMetatable));
}

auto PushCharacterId(lua_State* state, const physics::CharacterId character_id)
  -> int
{
  auto* userdata = static_cast<PhysicsCharacterIdUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsCharacterIdUserdata)));
  userdata->character_id = character_id;
  SetMetatable(state, kPhysicsCharacterIdMetatable);
  return 1;
}

auto CheckCharacterId(lua_State* state, const int index)
  -> PhysicsCharacterIdUserdata*
{
  return static_cast<PhysicsCharacterIdUserdata*>(
    luaL_checkudata(state, index, kPhysicsCharacterIdMetatable));
}

auto PushAggregateHandle(lua_State* state, const physics::WorldId world_id,
  const physics::AggregateId aggregate_id) -> int
{
  auto* userdata = static_cast<PhysicsAggregateHandleUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsAggregateHandleUserdata)));
  *userdata = PhysicsAggregateHandleUserdata {
    .world_id = world_id,
    .aggregate_id = aggregate_id,
  };
  SetMetatable(state, kPhysicsAggregateHandleMetatable);
  return 1;
}

auto CheckAggregateHandle(lua_State* state, const int index)
  -> PhysicsAggregateHandleUserdata*
{
  return static_cast<PhysicsAggregateHandleUserdata*>(
    luaL_checkudata(state, index, kPhysicsAggregateHandleMetatable));
}

auto PushAggregateId(lua_State* state, const physics::AggregateId aggregate_id)
  -> int
{
  auto* userdata = static_cast<PhysicsAggregateIdUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsAggregateIdUserdata)));
  userdata->aggregate_id = aggregate_id;
  SetMetatable(state, kPhysicsAggregateIdMetatable);
  return 1;
}

auto CheckAggregateId(lua_State* state, const int index)
  -> PhysicsAggregateIdUserdata*
{
  return static_cast<PhysicsAggregateIdUserdata*>(
    luaL_checkudata(state, index, kPhysicsAggregateIdMetatable));
}

auto PushJointHandle(lua_State* state, const physics::WorldId world_id,
  const physics::JointId joint_id) -> int
{
  auto* userdata = static_cast<PhysicsJointHandleUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsJointHandleUserdata)));
  *userdata = PhysicsJointHandleUserdata {
    .world_id = world_id,
    .joint_id = joint_id,
  };
  SetMetatable(state, kPhysicsJointHandleMetatable);
  return 1;
}

auto CheckJointHandle(lua_State* state, const int index)
  -> PhysicsJointHandleUserdata*
{
  return static_cast<PhysicsJointHandleUserdata*>(
    luaL_checkudata(state, index, kPhysicsJointHandleMetatable));
}

auto PushJointId(lua_State* state, const physics::JointId joint_id) -> int
{
  auto* userdata = static_cast<PhysicsJointIdUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsJointIdUserdata)));
  userdata->joint_id = joint_id;
  SetMetatable(state, kPhysicsJointIdMetatable);
  return 1;
}

auto CheckJointId(lua_State* state, const int index) -> PhysicsJointIdUserdata*
{
  return static_cast<PhysicsJointIdUserdata*>(
    luaL_checkudata(state, index, kPhysicsJointIdMetatable));
}

auto PushShapeId(lua_State* state, const physics::ShapeId shape_id) -> int
{
  auto* userdata = static_cast<PhysicsShapeIdUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsShapeIdUserdata)));
  userdata->shape_id = shape_id;
  SetMetatable(state, kPhysicsShapeIdMetatable);
  return 1;
}

auto CheckShapeId(lua_State* state, const int index) -> PhysicsShapeIdUserdata*
{
  return static_cast<PhysicsShapeIdUserdata*>(
    luaL_checkudata(state, index, kPhysicsShapeIdMetatable));
}

auto PushShapeInstanceId(
  lua_State* state, const physics::ShapeInstanceId shape_instance_id) -> int
{
  auto* userdata = static_cast<PhysicsShapeInstanceIdUserdata*>(
    lua_newuserdata(state, sizeof(PhysicsShapeInstanceIdUserdata)));
  userdata->shape_instance_id = shape_instance_id;
  SetMetatable(state, kPhysicsShapeInstanceIdMetatable);
  return 1;
}

auto CheckShapeInstanceId(lua_State* state, const int index)
  -> PhysicsShapeInstanceIdUserdata*
{
  return static_cast<PhysicsShapeInstanceIdUserdata*>(
    luaL_checkudata(state, index, kPhysicsShapeInstanceIdMetatable));
}

auto GetPhysicsModule(lua_State* state) -> physics::PhysicsModule*
{
  const auto engine = GetActiveEngine(state);
  if (engine == nullptr) {
    return nullptr;
  }
  auto physics_opt = engine->GetModule<physics::PhysicsModule>();
  if (!physics_opt.has_value()) {
    return nullptr;
  }
  return &physics_opt->get();
}

auto GetPhysicsWorldId(lua_State* state) -> std::optional<physics::WorldId>
{
  auto* physics_module = GetPhysicsModule(state);
  if (physics_module == nullptr) {
    return std::nullopt;
  }
  const auto world_id = physics_module->GetWorldId();
  if (!physics::IsValid(world_id)) {
    return std::nullopt;
  }
  return world_id;
}

auto IsPhysicsScriptablePhase(lua_State* state) -> bool
{
  return GetActiveEventPhase(state) != "fixed_simulation";
}

auto IsAttachAllowed(lua_State* state) -> bool
{
  if (!IsPhysicsScriptablePhase(state)) {
    return false;
  }
  const auto phase = GetActiveEventPhase(state);
  return phase == "gameplay" || phase == "scene_mutation";
}

auto IsCommandAllowed(lua_State* state) -> bool
{
  if (!IsPhysicsScriptablePhase(state)) {
    return false;
  }
  return GetActiveEventPhase(state) == "gameplay";
}

auto IsAggregateMutationAllowed(lua_State* state) -> bool
{
  if (!IsPhysicsScriptablePhase(state)) {
    return false;
  }
  return GetActiveEventPhase(state) == "gameplay";
}

auto IsEventDrainAllowed(lua_State* state) -> bool
{
  if (!IsPhysicsScriptablePhase(state)) {
    return false;
  }
  return GetActiveEventPhase(state) == "scene_mutation";
}

auto ParseCollisionShape(lua_State* state, const int table_index)
  -> physics::CollisionShape
{
  const auto abs_index = lua_absindex(state, table_index);
  luaL_checktype(state, abs_index, LUA_TTABLE);

  lua_getfield(state, abs_index, "type");
  const auto* shape_type = luaL_checkstring(state, -1);
  const auto shape_type_view
    = std::string_view(shape_type == nullptr ? "" : shape_type);
  lua_pop(state, 1);

  if (shape_type_view == "sphere") {
    const auto radius
      = ReadOptionalFloatField(state, abs_index, "radius", 0.5F);
    return physics::SphereShape { .radius = radius };
  }
  if (shape_type_view == "box") {
    lua_getfield(state, abs_index, "extents");
    Vec3 extents { 0.5F, 0.5F, 0.5F };
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, extents)) {
        lua_pop(state, 1);
        luaL_error(state, "shape.extents must be a vector");
        return physics::SphereShape {};
      }
      lua_pop(state, 1);
    } else {
      lua_pop(state, 1);
      lua_getfield(state, abs_index, "half_extents");
      if (lua_isnil(state, -1) == 0) {
        if (!TryCheckVec3(state, -1, extents)) {
          lua_pop(state, 1);
          luaL_error(state, "shape.half_extents must be a vector");
          return physics::SphereShape {};
        }
      }
      lua_pop(state, 1);
    }
    return physics::BoxShape { .extents = extents };
  }
  if (shape_type_view == "capsule") {
    const auto radius
      = ReadOptionalFloatField(state, abs_index, "radius", 0.5F);
    const auto half_height
      = ReadOptionalFloatField(state, abs_index, "half_height", 1.0F);
    return physics::CapsuleShape {
      .radius = radius,
      .half_height = half_height,
    };
  }
  if (shape_type_view == "cylinder") {
    const auto radius
      = ReadOptionalFloatField(state, abs_index, "radius", 0.5F);
    const auto half_height
      = ReadOptionalFloatField(state, abs_index, "half_height", 1.0F);
    return physics::CylinderShape {
      .radius = radius,
      .half_height = half_height,
    };
  }
  if (shape_type_view == "cone") {
    const auto radius
      = ReadOptionalFloatField(state, abs_index, "radius", 0.5F);
    const auto half_height
      = ReadOptionalFloatField(state, abs_index, "half_height", 1.0F);
    return physics::ConeShape {
      .radius = radius,
      .half_height = half_height,
      .cooked_payload = ParseCookedPayload(
        state, abs_index, physics::ShapePayloadType::kConvex),
    };
  }
  if (shape_type_view == "convex_hull") {
    return physics::ConvexHullShape {
      .cooked_payload = ParseCookedPayload(
        state, abs_index, physics::ShapePayloadType::kConvex),
    };
  }
  if (shape_type_view == "triangle_mesh") {
    return physics::TriangleMeshShape {
      .cooked_payload
      = ParseCookedPayload(state, abs_index, physics::ShapePayloadType::kMesh),
    };
  }
  if (shape_type_view == "height_field") {
    return physics::HeightFieldShape {
      .cooked_payload = ParseCookedPayload(
        state, abs_index, physics::ShapePayloadType::kHeightField),
    };
  }
  if (shape_type_view == "plane") {
    lua_getfield(state, abs_index, "normal");
    auto normal = Vec3 { 0.0F, 0.0F, 1.0F };
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, normal)) {
        lua_pop(state, 1);
        luaL_error(state, "shape.normal must be a vector");
        return physics::SphereShape {};
      }
    }
    lua_pop(state, 1);
    const auto distance
      = ReadOptionalFloatField(state, abs_index, "distance", 0.0F);
    return physics::PlaneShape {
      .normal = normal,
      .distance = distance,
    };
  }
  if (shape_type_view == "world_boundary") {
    lua_getfield(state, abs_index, "limits_min");
    Vec3 limits_min {};
    if (!TryCheckVec3(state, -1, limits_min)) {
      lua_pop(state, 1);
      luaL_error(state, "shape.limits_min must be a vector");
      return physics::SphereShape {};
    }
    lua_pop(state, 1);

    lua_getfield(state, abs_index, "limits_max");
    Vec3 limits_max {};
    if (!TryCheckVec3(state, -1, limits_max)) {
      lua_pop(state, 1);
      luaL_error(state, "shape.limits_max must be a vector");
      return physics::SphereShape {};
    }
    lua_pop(state, 1);

    return physics::WorldBoundaryShape {
      .mode = ParseWorldBoundaryMode(state, abs_index),
      .limits_min = limits_min,
      .limits_max = limits_max,
    };
  }
  if (shape_type_view == "compound") {
    return physics::CompoundShape {
      .cooked_payload = ParseCookedPayload(
        state, abs_index, physics::ShapePayloadType::kCompound),
    };
  }

  luaL_error(state, "unsupported shape.type '%s'", shape_type);
  return physics::SphereShape {};
}

auto ParseBodyIdArray(lua_State* state, const int table_index)
  -> std::vector<physics::BodyId>
{
  const auto abs_index = lua_absindex(state, table_index);
  luaL_checktype(state, abs_index, LUA_TTABLE);

  const auto len = lua_objlen(state, abs_index);
  std::vector<physics::BodyId> body_ids {};
  body_ids.reserve(len);
  for (int i = 1; i <= len; ++i) {
    lua_rawgeti(state, abs_index, i);
    const auto* body_id_userdata = CheckBodyId(state, -1);
    body_ids.push_back(body_id_userdata->body_id);
    lua_pop(state, 1);
  }
  return body_ids;
}

auto ParseBodyIdOrHandle(lua_State* state, const int index) -> physics::BodyId
{
  if (lua_isuserdata(state, index) == 0) {
    luaL_argerror(state, index, "expected BodyId or BodyHandle userdata");
    return physics::kInvalidBodyId;
  }

  if (lua_getmetatable(state, index) == 0) {
    luaL_argerror(state, index, "expected BodyId or BodyHandle userdata");
    return physics::kInvalidBodyId;
  }
  const int mt_index = lua_gettop(state);

  luaL_getmetatable(state, kPhysicsBodyIdMetatable);
  const bool is_body_id = lua_rawequal(state, mt_index, -1) != 0;
  lua_pop(state, 1);
  if (is_body_id) {
    const auto* id = CheckBodyId(state, index);
    lua_pop(state, 1);
    return id->body_id;
  }

  luaL_getmetatable(state, kPhysicsBodyHandleMetatable);
  const bool is_body_handle = lua_rawequal(state, mt_index, -1) != 0;
  lua_pop(state, 1);
  if (is_body_handle) {
    const auto* handle = CheckBodyHandle(state, index);
    lua_pop(state, 1);
    return handle->body_id;
  }

  lua_pop(state, 1);
  luaL_argerror(state, index, "expected BodyId or BodyHandle userdata");
  return physics::kInvalidBodyId;
}

auto ParseBodyIdOrHandleArray(lua_State* state, const int table_index)
  -> std::vector<physics::BodyId>
{
  const auto abs_index = lua_absindex(state, table_index);
  luaL_checktype(state, abs_index, LUA_TTABLE);

  const auto len = lua_objlen(state, abs_index);
  std::vector<physics::BodyId> body_ids {};
  body_ids.reserve(len);
  for (int i = 1; i <= len; ++i) {
    lua_rawgeti(state, abs_index, i);
    body_ids.push_back(ParseBodyIdOrHandle(state, -1));
    lua_pop(state, 1);
  }
  return body_ids;
}

} // namespace oxygen::scripting::bindings
