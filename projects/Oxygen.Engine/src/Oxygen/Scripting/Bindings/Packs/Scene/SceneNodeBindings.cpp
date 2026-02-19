//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeComponentBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr const char* kSceneNodeMetatable = "oxygen.scene.node";
  constexpr const char* kQuatMetatable = "oxygen.quat";

  struct SceneNodeUserdata {
    scene::SceneNode node;
  };

  auto IsNodeInActiveScene(const scene::SceneNode& node,
    const observer_ptr<scene::Scene> scene_ref) -> bool
  {
    if ((scene_ref == nullptr) || !node.IsValid()) {
      return false;
    }
    return node.GetHandle().GetSceneId() == scene_ref->GetId();
  }

  auto IsMutationAllowedPhase(lua_State* state) -> bool
  {
    const auto phase = GetActiveEventPhase(state);
    return phase == "scene_mutation" || phase == "frame_start";
  }

  auto SceneNodeGc(lua_State* state) -> int
  {
    auto* ud = static_cast<SceneNodeUserdata*>(lua_touserdata(state, 1));
    ud->node.~SceneNode();
    return 0;
  }

  auto SceneNodeToString(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushliteral(state, "SceneNode(Invalid)");
      return 1;
    }
    const auto s = scene::to_string(*node);
    lua_pushlstring(state, s.c_str(), s.size());
    return 1;
  }

  auto SceneNodeEq(lua_State* state) -> int
  {
    auto* n1 = CheckSceneNode(state, 1);
    auto* n2 = CheckSceneNode(state, 2);
    lua_pushboolean(state, n1->GetHandle() == n2->GetHandle() ? 1 : 0);
    return 1;
  }

  auto SceneNodeIsAlive(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    lua_pushboolean(state, node->IsAlive() ? 1 : 0);
    return 1;
  }

  auto SceneNodeRuntimeId(lua_State* state) -> int
  {
    const auto* node = CheckSceneNode(state, 1);
    const auto& handle = node->GetHandle();

    lua_createtable(state, 0, 2);
    lua_pushinteger(state, static_cast<lua_Integer>(handle.GetSceneId()));
    lua_setfield(state, -2, "scene_id");
    lua_pushinteger(state, static_cast<lua_Integer>(handle.Index()));
    lua_setfield(state, -2, "node_index");
    return 1;
  }

  auto SceneNodeGetName(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    const auto name = node->GetName();
    lua_pushlstring(state, name.c_str(), name.size());
    return 1;
  }

  auto SceneNodeSetName(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    size_t len = 0;
    const char* name = luaL_checklstring(state, 2, &len);
    if (!node->IsAlive()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->SetName(std::string(name, len)) ? 1 : 0);
    return 1;
  }

  auto SceneNodeGetParent(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    const auto parent = node->GetParent();
    if (!parent.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneNode(state, *parent);
  }

  auto SceneNodeSetParent(lua_State* state) -> int
  {
    if (!IsMutationAllowedPhase(state)) {
      LOG_F(WARNING,
        "scene.node.set_parent rejected outside scene_mutation/frame_start "
        "phase (active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushboolean(state, 0);
      return 1;
    }

    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushboolean(state, 0);
      return 1;
    }

    if (lua_isnil(state, 2) != 0) {
      lua_pushboolean(state, 0);
      return 1;
    }

    auto* new_parent = CheckSceneNode(state, 2);
    if (!new_parent->IsAlive()) {
      lua_pushboolean(state, 0);
      return 1;
    }

    bool preserve_world = true;
    if ((lua_gettop(state) >= 3) && lua_isboolean(state, 3)) {
      preserve_world = lua_toboolean(state, 3) != 0;
    }

    auto scene_ref = GetScene(state);
    if (!IsNodeInActiveScene(*node, scene_ref)
      || !IsNodeInActiveScene(*new_parent, scene_ref)) {
      lua_pushboolean(state, 0);
      return 1;
    }

    lua_pushboolean(state,
      scene_ref->ReparentNode(*node, *new_parent, preserve_world) ? 1 : 0);
    return 1;
  }

  auto SceneNodeIsRoot(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    // NOLINTNEXTLINE(*-nested-conditional-operator)
    lua_pushboolean(state, node->IsAlive() ? (node->IsRoot() ? 1 : 0) : 0);
    return 1;
  }

  auto SceneNodeHasParent(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    // NOLINTNEXTLINE(*-nested-conditional-operator)
    lua_pushboolean(state, node->IsAlive() ? (node->HasParent() ? 1 : 0) : 0);
    return 1;
  }

  auto SceneNodeHasChildren(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    // NOLINTNEXTLINE(*-nested-conditional-operator)
    lua_pushboolean(state, node->IsAlive() ? (node->HasChildren() ? 1 : 0) : 0);
    return 1;
  }

  auto SceneNodeGetFirstChild(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    const auto child = node->GetFirstChild();
    if (!child.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneNode(state, *child);
  }

  auto SceneNodeGetNextSibling(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    const auto sibling = node->GetNextSibling();
    if (!sibling.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneNode(state, *sibling);
  }

  auto SceneNodeGetPrevSibling(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    const auto sibling = node->GetPrevSibling();
    if (!sibling.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneNode(state, *sibling);
  }

  auto SceneNodeGetChildren(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }

    lua_newtable(state);
    auto child = node->GetFirstChild();
    int lua_index = 1;
    while (child.has_value()) {
      PushSceneNode(state, *child);
      lua_rawseti(state, -2, lua_index);
      ++lua_index;
      child = child->GetNextSibling();
    }
    return 1;
  }

  auto SceneNodeDestroy(lua_State* state) -> int
  {
    if (!IsMutationAllowedPhase(state)) {
      LOG_F(WARNING,
        "scene.node.destroy rejected outside scene_mutation/frame_start phase "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushboolean(state, 0);
      return 1;
    }

    auto* node = CheckSceneNode(state, 1);
    auto scene_ref = GetScene(state);
    if (!IsNodeInActiveScene(*node, scene_ref)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, scene_ref->DestroyNode(*node) ? 1 : 0);
    return 1;
  }

  auto SceneNodeGetLocalPosition(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = node->GetTransform().GetLocalPosition();
    if (!v.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, *v);
  }

  auto SceneNodeSetLocalPosition(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = CheckVec3(state, 2, "set_local_position expects vector");
    lua_pushboolean(state, node->GetTransform().SetLocalPosition(v) ? 1 : 0);
    return 1;
  }

  auto SceneNodeGetLocalRotation(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = node->GetTransform().GetLocalRotation();
    if (!v.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushQuat(state, v->x, v->y, v->z, v->w);
  }

  auto SceneNodeSetLocalRotation(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto* q = CheckQuat(state, 2);
    lua_pushboolean(state,
      node->GetTransform().SetLocalRotation(Quat(q->w, q->x, q->y, q->z)) ? 1
                                                                          : 0);
    return 1;
  }

  auto SceneNodeGetLocalScale(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = node->GetTransform().GetLocalScale();
    if (!v.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, *v);
  }

  auto SceneNodeSetLocalScale(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = CheckVec3(state, 2, "set_local_scale expects vector");
    lua_pushboolean(state, node->GetTransform().SetLocalScale(v) ? 1 : 0);
    return 1;
  }

  auto SceneNodeSetLocalTransform(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto pos
      = CheckVec3(state, 2, "set_local_transform expects vec3 pos");
    const auto* rot = CheckQuat(state, 3);
    const auto scale
      = CheckVec3(state, 4, "set_local_transform expects vec3 scale");
    lua_pushboolean(state,
      node->GetTransform().SetLocalTransform(
        pos, Quat(rot->w, rot->x, rot->y, rot->z), scale)
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeTranslate(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto offset = CheckVec3(state, 2, "translate expects offset vector");
    bool local_space = true;
    if ((lua_gettop(state) >= 3) && lua_isboolean(state, 3)) {
      local_space = lua_toboolean(state, 3) != 0;
    }
    lua_pushboolean(
      state, node->GetTransform().Translate(offset, local_space) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRotate(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto* delta = CheckQuat(state, 2);
    bool local_space = true;
    if ((lua_gettop(state) >= 3) && lua_isboolean(state, 3)) {
      local_space = lua_toboolean(state, 3) != 0;
    }
    lua_pushboolean(state,
      node->GetTransform().Rotate(
        Quat(delta->w, delta->x, delta->y, delta->z), local_space)
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeScaleBy(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto factor = CheckVec3(state, 2, "scale_by expects vector");
    lua_pushboolean(state, node->GetTransform().Scale(factor) ? 1 : 0);
    return 1;
  }

  auto SceneNodeGetWorldPosition(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = node->GetTransform().GetWorldPosition();
    if (!v.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, *v);
  }

  auto SceneNodeGetWorldRotation(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = node->GetTransform().GetWorldRotation();
    if (!v.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushQuat(state, v->x, v->y, v->z, v->w);
  }

  auto SceneNodeGetWorldScale(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto v = node->GetTransform().GetWorldScale();
    if (!v.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, *v);
  }

  auto SceneNodeLookAt(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto target = CheckVec3(state, 2, "look_at expects target vector");
    Vec3 up = oxygen::space::look::Up;
    if (lua_gettop(state) >= 3) {
      up = CheckVec3(state, 3, "look_at expects up vector");
    }
    lua_pushboolean(state, node->GetTransform().LookAt(target, up) ? 1 : 0);
    return 1;
  }

  auto CallNodeMethodByName(lua_State* state, const char* method) -> int
  {
    if (lua_getmetatable(state, 1) == 0) {
      lua_pushnil(state);
      return 1;
    }
    lua_getfield(state, -1, method);
    lua_remove(state, -2);
    if (!lua_isfunction(state, -1)) {
      lua_pop(state, 1);
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    lua_call(state, 1, 1);
    return 1;
  }

  auto SceneNodeIndex(lua_State* state) -> int
  {
    const auto* key = lua_tostring(state, 2);
    if (key == nullptr) {
      if (lua_getmetatable(state, 1) == 0) {
        lua_pushnil(state);
        return 1;
      }
      lua_pushvalue(state, 2);
      lua_rawget(state, -2);
      lua_remove(state, -2);
      return 1;
    }

    constexpr std::array<std::pair<std::string_view, const char*>, 11> aliases {
      { { "name", "get_name" }, { "parent", "get_parent" },
        { "local_position", "get_local_position" },
        { "local_rotation", "get_local_rotation" },
        { "local_scale", "get_local_scale" },
        { "world_position", "get_world_position" },
        { "world_rotation", "get_world_rotation" },
        { "world_scale", "get_world_scale" }, { "camera_component", "camera" },
        { "light_component", "light" },
        { "renderable_component", "renderable" } }
    };

    const std::string_view key_sv(key);
    for (const auto& [alias, method] : aliases) {
      if (alias == key_sv) {
        return CallNodeMethodByName(state, method);
      }
    }
    if (key_sv == "scripting_component") {
      return CallNodeMethodByName(state, "scripting");
    }

    if (lua_getmetatable(state, 1) == 0) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 2);
    lua_rawget(state, -2);
    lua_remove(state, -2);
    return 1;
  }

  auto SceneNodeNewIndex(lua_State* state) -> int
  {
    const auto* key = lua_tostring(state, 2);
    if (key == nullptr) {
      luaL_error(state, "SceneNode property key must be a string");
      return 0;
    }

    const std::string_view key_sv(key);
    if (key_sv == "name") {
      lua_settop(state, 3);
      lua_pushcfunction(state, SceneNodeSetName, "set_name");
      lua_pushvalue(state, 1);
      lua_pushvalue(state, 3);
      lua_call(state, 2, 1);
      lua_pop(state, 1);
      return 0;
    }
    if (key_sv == "parent") {
      lua_pushcfunction(state, SceneNodeSetParent, "set_parent");
      lua_pushvalue(state, 1);
      lua_pushvalue(state, 3);
      lua_call(state, 2, 1);
      lua_pop(state, 1);
      return 0;
    }
    if (key_sv == "local_position") {
      lua_pushcfunction(state, SceneNodeSetLocalPosition, "set_local_position");
      lua_pushvalue(state, 1);
      lua_pushvalue(state, 3);
      lua_call(state, 2, 1);
      lua_pop(state, 1);
      return 0;
    }
    if (key_sv == "local_rotation") {
      lua_pushcfunction(state, SceneNodeSetLocalRotation, "set_local_rotation");
      lua_pushvalue(state, 1);
      lua_pushvalue(state, 3);
      lua_call(state, 2, 1);
      lua_pop(state, 1);
      return 0;
    }
    if (key_sv == "local_scale") {
      lua_pushcfunction(state, SceneNodeSetLocalScale, "set_local_scale");
      lua_pushvalue(state, 1);
      lua_pushvalue(state, 3);
      lua_call(state, 2, 1);
      lua_pop(state, 1);
      return 0;
    }

    if ((key_sv == "is_alive") || (key_sv == "world_position")
      || (key_sv == "world_rotation") || (key_sv == "world_scale")
      || (key_sv == "camera_component") || (key_sv == "light_component")
      || (key_sv == "renderable_component")
      || (key_sv == "scripting_component")) {
      luaL_error(state, "SceneNode property '%s' is read-only", key);
      return 0;
    }

    luaL_error(state, "Unknown SceneNode property '%s'", key);
    return 0;
  }
} // namespace

auto RegisterSceneNodeMetatable(lua_State* state) -> void
{
  luaL_newmetatable(state, kSceneNodeMetatable);
  lua_pushcfunction(state, SceneNodeGc, "__gc");
  lua_setfield(state, -2, "__gc");

  lua_pushcfunction(state, SceneNodeToString, "__tostring");
  lua_setfield(state, -2, "__tostring");

  lua_pushcfunction(state, SceneNodeEq, "__eq");
  lua_setfield(state, -2, "__eq");

  lua_pushcfunction(state, SceneNodeIndex, "__index");
  lua_setfield(state, -2, "__index");
  lua_pushcfunction(state, SceneNodeNewIndex, "__newindex");
  lua_setfield(state, -2, "__newindex");

  constexpr std::array<luaL_Reg, 28> methods {
    { { .name = "is_alive", .func = SceneNodeIsAlive },
      { .name = "runtime_id", .func = SceneNodeRuntimeId },
      { .name = "get_name", .func = SceneNodeGetName },
      { .name = "set_name", .func = SceneNodeSetName },
      { .name = "to_string", .func = SceneNodeToString },
      { .name = "get_parent", .func = SceneNodeGetParent },
      { .name = "set_parent", .func = SceneNodeSetParent },
      { .name = "is_root", .func = SceneNodeIsRoot },
      { .name = "has_parent", .func = SceneNodeHasParent },
      { .name = "has_children", .func = SceneNodeHasChildren },
      { .name = "get_children", .func = SceneNodeGetChildren },
      { .name = "get_first_child", .func = SceneNodeGetFirstChild },
      { .name = "get_next_sibling", .func = SceneNodeGetNextSibling },
      { .name = "get_prev_sibling", .func = SceneNodeGetPrevSibling },
      { .name = "destroy", .func = SceneNodeDestroy },
      { .name = "get_local_position", .func = SceneNodeGetLocalPosition },
      { .name = "set_local_position", .func = SceneNodeSetLocalPosition },
      { .name = "get_local_rotation", .func = SceneNodeGetLocalRotation },
      { .name = "set_local_rotation", .func = SceneNodeSetLocalRotation },
      { .name = "get_local_scale", .func = SceneNodeGetLocalScale },
      { .name = "set_local_scale", .func = SceneNodeSetLocalScale },
      { .name = "set_local_transform", .func = SceneNodeSetLocalTransform },
      { .name = "translate", .func = SceneNodeTranslate },
      { .name = "rotate", .func = SceneNodeRotate },
      { .name = "scale_by", .func = SceneNodeScaleBy },
      { .name = "get_world_position", .func = SceneNodeGetWorldPosition },
      { .name = "get_world_rotation", .func = SceneNodeGetWorldRotation },
      { .name = "get_world_scale", .func = SceneNodeGetWorldScale } }
  };

  for (const auto& reg : methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, -2, reg.name);
  }
  RegisterSceneNodeComponentMethods(state, -2);
  lua_pushcfunction(state, SceneNodeLookAt, "look_at");
  lua_setfield(state, -2, "look_at");
  lua_pop(state, 1);
}

auto CheckSceneNode(lua_State* state, const int index) -> scene::SceneNode*
{
  auto* ud = static_cast<SceneNodeUserdata*>(
    luaL_checkudata(state, index, kSceneNodeMetatable));
  return &ud->node;
}

auto PushSceneNode(lua_State* state, scene::SceneNode node) -> int
{
  void* data = lua_newuserdata(state, sizeof(SceneNodeUserdata));
  new (data) SceneNodeUserdata { std::move(node) };

  if (luaL_getmetatable(state, kSceneNodeMetatable) != 0) {
    lua_setmetatable(state, -2);
  } else {
    lua_pop(state, 1);
    luaL_error(state, "SceneNode metatable not found");
    return 0;
  }
  return 1;
}

auto GetScene(lua_State* state) -> observer_ptr<scene::Scene>
{
  const auto context = GetActiveFrameContext(state);
  if (context == nullptr) {
    return nullptr;
  }
  return context->GetScene();
}

auto CheckVec3(lua_State* state, const int index, const char* msg) -> Vec3
{
  if (!lua_isvector(state, index)) {
    luaL_error(state, "%s", msg);
    return {};
  }
  const float* v = lua_tovector(state, index);
  return { v[0], v[1], v[2] }; // NOLINT
}

auto PushVec3(lua_State* state, const Vec3& v) -> int
{
  lua_pushvector(state, v.x, v.y, v.z);
  return 1;
}

auto CheckQuat(lua_State* state, const int index) -> QuatUserdata*
{
  return static_cast<QuatUserdata*>(
    luaL_checkudata(state, index, kQuatMetatable));
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
auto PushQuat(lua_State* state, const float x, const float y, const float z,
  const float w) -> int
{
  auto* u
    = static_cast<QuatUserdata*>(lua_newuserdata(state, sizeof(QuatUserdata)));
  u->x = x;
  u->y = y;
  u->z = z;
  u->w = w;
  luaL_getmetatable(state, kQuatMetatable);
  if (lua_isnil(state, -1) != 0) {
    lua_pop(state, 1);
    luaL_error(state, "Quat metatable not found");
    return 0;
  }
  lua_setmetatable(state, -2);
  return 1;
}

} // namespace oxygen::scripting::bindings
