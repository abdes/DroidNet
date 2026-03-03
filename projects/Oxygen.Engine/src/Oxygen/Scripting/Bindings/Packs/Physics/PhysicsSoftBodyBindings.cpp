//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsSoftBodyBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.soft_body.{} rejected during fixed_simulation",
      op_name);
    lua_pushnil(state);
    return true;
  }

  auto RejectFixedSimulationWithBool(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.soft_body.{} rejected during fixed_simulation",
      op_name);
    lua_pushboolean(state, 0);
    return true;
  }

  auto RejectMutationOutsideGameplayWithNil(
    lua_State* state, const char* op_name) -> bool
  {
    if (IsAggregateMutationAllowed(state)) {
      return false;
    }
    LOG_F(WARNING,
      "physics.soft_body.{} rejected outside gameplay phase "
      "(active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushnil(state);
    return true;
  }

  auto RejectMutationOutsideGameplayWithBool(
    lua_State* state, const char* op_name) -> bool
  {
    if (IsAggregateMutationAllowed(state)) {
      return false;
    }
    LOG_F(WARNING,
      "physics.soft_body.{} rejected outside gameplay phase "
      "(active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushboolean(state, 0);
    return true;
  }

  auto ReadFiniteField(lua_State* state, const int table_index,
    const char* field_name, const float fallback) -> float
  {
    lua_getfield(state, table_index, field_name);
    float value = fallback;
    if (lua_isnil(state, -1) == 0) {
      value = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);
    if (!std::isfinite(value)) {
      luaL_error(state, "soft_body field '%s' must be finite", field_name);
      return fallback;
    }
    return value;
  }

  auto ParseTetherMode(lua_State* state, const int table_index)
    -> physics::softbody::SoftBodyTetherMode
  {
    lua_getfield(state, table_index, "tether_mode");
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      return physics::softbody::SoftBodyTetherMode::kNone;
    }
    const auto* text = luaL_checkstring(state, -1);
    const auto view = std::string_view(text == nullptr ? "" : text);
    lua_pop(state, 1);
    if (view == "none") {
      return physics::softbody::SoftBodyTetherMode::kNone;
    }
    if (view == "euclidean") {
      return physics::softbody::SoftBodyTetherMode::kEuclidean;
    }
    if (view == "geodesic") {
      return physics::softbody::SoftBodyTetherMode::kGeodesic;
    }
    luaL_error(state,
      "soft_body.material_params.tether_mode must be "
      "'none'|'euclidean'|'geodesic'");
    return physics::softbody::SoftBodyTetherMode::kNone;
  }

  auto ParseMaterialParams(lua_State* state, const int table_index)
    -> physics::softbody::SoftBodyMaterialParams
  {
    const auto abs = lua_absindex(state, table_index);
    luaL_checktype(state, abs, LUA_TTABLE);
    return physics::softbody::SoftBodyMaterialParams {
      .stiffness = ReadFiniteField(state, abs, "stiffness", 0.0F),
      .damping = ReadFiniteField(state, abs, "damping", 0.0F),
      .edge_compliance = ReadFiniteField(state, abs, "edge_compliance", 0.0F),
      .shear_compliance = ReadFiniteField(state, abs, "shear_compliance", 0.0F),
      .bend_compliance = ReadFiniteField(
        state, abs, "bend_compliance", (std::numeric_limits<float>::max)()),
      .tether_mode = ParseTetherMode(state, abs),
      .tether_max_distance_multiplier
      = ReadFiniteField(state, abs, "tether_max_distance_multiplier", 1.0F),
    };
  }

  auto ParseSoftBodyDesc(lua_State* state, const int arg_index)
    -> physics::softbody::SoftBodyDesc
  {
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    physics::softbody::SoftBodyDesc desc {};

    lua_getfield(state, desc_index, "anchor_body_id");
    if (lua_isnil(state, -1) == 0) {
      desc.anchor_body_id = ParseBodyIdOrHandle(state, -1);
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "cluster_count");
    desc.cluster_count = static_cast<uint32_t>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    if (desc.cluster_count == 0U) {
      luaL_error(state, "soft_body.cluster_count must be > 0");
      return {};
    }

    lua_getfield(state, desc_index, "material_params");
    if (lua_isnil(state, -1) == 0) {
      desc.material_params = ParseMaterialParams(state, -1);
    }
    lua_pop(state, 1);

    return desc;
  }

  auto LuaSoftBodyCreate(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "create")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithNil(state, "create")) {
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto desc = ParseSoftBodyDesc(state, 1);
    const auto result
      = physics_module->SoftBodies().CreateSoftBody(*world_id_opt, desc);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushAggregateHandle(state, *world_id_opt, result.value());
  }

  auto PushSoftBodyHandleForNode(lua_State* state,
    physics::PhysicsModule& physics_module, const physics::WorldId world_id,
    const scene::NodeHandle node_handle) -> int
  {
    const auto aggregate_id = physics_module.GetAggregateIdForNode(node_handle);
    if (!physics::IsValid(aggregate_id)) {
      lua_pushnil(state);
      return 1;
    }

    const auto state_result
      = physics_module.SoftBodies().GetState(world_id, aggregate_id);
    if (!state_result.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    return PushAggregateHandle(state, world_id, aggregate_id);
  }

  auto LuaSoftBodyGetExact(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_exact")) {
      return 1;
    }

    const auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    return PushSoftBodyHandleForNode(
      state, *physics_module, *world_id_opt, node->GetHandle());
  }

  auto LuaSoftBodyFindInAncestors(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "find_in_ancestors")) {
      return 1;
    }

    const auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr || !node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    auto current = std::optional<scene::SceneNode> { *node };
    while (current.has_value() && current->IsAlive()) {
      const auto pushed = PushSoftBodyHandleForNode(
        state, *physics_module, *world_id_opt, current->GetHandle());
      if (lua_isnil(state, -1) == 0) {
        return pushed;
      }
      lua_pop(state, 1);
      current = current->GetParent();
    }

    lua_pushnil(state);
    return 1;
  }

  auto LuaSoftBodyDestroy(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "destroy")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "destroy")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->SoftBodies().DestroySoftBody(
      handle->world_id, handle->aggregate_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaSoftBodySetMaterialParams(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "set_material_params")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "set_material_params")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    const auto params = ParseMaterialParams(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->SoftBodies().SetMaterialParams(
      handle->world_id, handle->aggregate_id, params);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaSoftBodyGetState(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_state")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto result = physics_module->SoftBodies().GetState(
      handle->world_id, handle->aggregate_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    lua_createtable(state, 0, 2);
    PushVec3(state, result.value().center_of_mass);
    lua_setfield(state, -2, "center_of_mass");
    lua_pushboolean(state, result.value().sleeping ? 1 : 0);
    lua_setfield(state, -2, "sleeping");
    return 1;
  }

  auto LuaSoftBodyGetAuthority(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_authority")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto result = physics_module->SoftBodies().GetAuthority(
      handle->world_id, handle->aggregate_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto text = physics::aggregate::to_string(result.value());
    lua_pushlstring(state, text.data(), text.size());
    return 1;
  }

  auto LuaSoftBodyFlushStructuralChanges(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "flush_structural_changes")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithNil(
          state, "flush_structural_changes")) {
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto result
      = physics_module->SoftBodies().FlushStructuralChanges(*world_id_opt);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(result.value()));
    return 1;
  }

} // namespace

auto RegisterPhysicsSoftBodyBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto soft_body_index
    = PushOxygenSubtable(state, physics_index, "soft_body");

  lua_pushcfunction(state, LuaSoftBodyCreate, "physics.soft_body.create");
  lua_setfield(state, soft_body_index, "create");
  lua_pushcfunction(state, LuaSoftBodyGetExact, "physics.soft_body.get_exact");
  lua_setfield(state, soft_body_index, "get_exact");
  lua_pushcfunction(
    state, LuaSoftBodyFindInAncestors, "physics.soft_body.find_in_ancestors");
  lua_setfield(state, soft_body_index, "find_in_ancestors");
  lua_pushcfunction(state, LuaSoftBodyDestroy, "physics.soft_body.destroy");
  lua_setfield(state, soft_body_index, "destroy");
  lua_pushcfunction(state, LuaSoftBodySetMaterialParams,
    "physics.soft_body.set_material_params");
  lua_setfield(state, soft_body_index, "set_material_params");
  lua_pushcfunction(state, LuaSoftBodyGetState, "physics.soft_body.get_state");
  lua_setfield(state, soft_body_index, "get_state");
  lua_pushcfunction(
    state, LuaSoftBodyGetAuthority, "physics.soft_body.get_authority");
  lua_setfield(state, soft_body_index, "get_authority");
  lua_pushcfunction(state, LuaSoftBodyFlushStructuralChanges,
    "physics.soft_body.flush_structural_changes");
  lua_setfield(state, soft_body_index, "flush_structural_changes");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
