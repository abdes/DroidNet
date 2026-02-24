//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneQueryBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto IsMutationAllowedPhase(lua_State* state) -> bool
  {
    const auto phase = GetActiveEventPhase(state);
    return phase == "scene_mutation";
  }

  auto RejectMutationOutsideScenePhase(
    lua_State* state, const char* operation_name) -> bool
  {
    if (IsMutationAllowedPhase(state)) {
      return false;
    }
    LOG_F(WARNING,
      "scene.{} rejected outside scene_mutation phase "
      "(active_phase='{}')",
      operation_name, GetActiveEventPhase(state));
    return true;
  }

  auto PushSceneNodeArray(
    lua_State* state, const std::vector<scene::SceneNode>& nodes) -> int
  {
    lua_createtable(state, static_cast<int>(nodes.size()), 0);
    int lua_index = 1;
    for (const auto& node : nodes) {
      PushSceneNode(state, node);
      lua_rawseti(state, -2, lua_index);
      ++lua_index;
    }
    return 1;
  }

  auto ApplyOptionalScope(
    lua_State* state, scene::SceneQuery& query, const int arg) -> bool
  {
    query.ResetTraversalScope();
    if ((lua_gettop(state) < arg) || lua_isnil(state, arg)) {
      return true;
    }
    auto* scope_node = TryCheckSceneNode(state, arg);
    if ((scope_node == nullptr) || !scope_node->IsAlive()) {
      return false;
    }
    query.AddToTraversalScope(*scope_node);
    return true;
  }

  auto IsNodeInActiveScene(const scene::SceneNode& node,
    const observer_ptr<scene::Scene> scene_ref) -> bool
  {
    if ((scene_ref == nullptr) || !node.IsValid()) {
      return false;
    }
    const auto handle = node.GetHandle();
    if (handle.GetSceneId() != scene_ref->GetId()) {
      return false;
    }
    const auto node_opt = scene_ref->GetNode(handle);
    if (!node_opt.has_value()) {
      return false;
    }
    return node_opt->IsAlive();
  }

  auto LuaSceneCurrentNode(lua_State* state) -> int
  {
    auto* ctx = GetBindingContextFromScriptArg(state, 1);
    if ((ctx != nullptr) && ctx->has_slot_context) {
      const auto frame_context = GetActiveFrameContext(state);
      if (frame_context == nullptr) {
        lua_pushnil(state);
        return 1;
      }
      const auto& scene = frame_context->GetScene();
      const auto node_opt = scene->GetNode(ctx->slot_context.node_handle);
      if (!node_opt || !node_opt->IsAlive()) {
        lua_pushnil(state);
        return 1;
      }
      const auto& node = *node_opt;
      const auto handle = node.GetHandle();
      static uint32_t s_last_scene_id = std::numeric_limits<uint32_t>::max();
      static uint32_t s_last_node_index = std::numeric_limits<uint32_t>::max();
      const auto scene_id = static_cast<uint32_t>(handle.GetSceneId());
      const auto node_index = static_cast<uint32_t>(handle.Index());
      if (scene_id != s_last_scene_id || node_index != s_last_node_index) {
        LOG_F(INFO,
          "scene.current_node switched: name='{}' scene_id={} node_index={}",
          node.GetName(), scene_id, node_index);
        s_last_scene_id = scene_id;
        s_last_node_index = node_index;
      }
      return PushSceneNode(state, node);
    }
    lua_pushnil(state);
    return 1;
  }

  auto LuaSceneParam(lua_State* state) -> int { return GetParamValue(state); }

  auto LuaSceneCreateNode(lua_State* state) -> int
  {
    if (RejectMutationOutsideScenePhase(state, "create_node")) {
      lua_pushnil(state);
      return 1;
    }

    size_t len = 0;
    const char* name = lua_tolstring(state, 1, &len);
    if (name == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    scene::SceneNode new_node;
    if ((lua_gettop(state) >= 2) && !lua_isnil(state, 2)) {
      auto* parent = TryCheckSceneNode(state, 2);
      if ((parent == nullptr) || !parent->IsAlive()
        || !IsNodeInActiveScene(*parent, scene_ref)) {
        lua_pushnil(state);
        return 1;
      }
      const auto node_opt
        = scene_ref->CreateChildNode(*parent, std::string(name, len));
      if (node_opt.has_value()) {
        new_node = *node_opt;
      }
    } else {
      new_node = scene_ref->CreateNode(std::string(name, len));
    }

    if (!new_node.IsValid()) {
      lua_pushnil(state);
      return 1;
    }
    if ((lua_gettop(state) >= 2) && !lua_isnil(state, 2)) {
      auto* parent = TryCheckSceneNode(state, 2);
      if (parent == nullptr) {
        lua_pushnil(state);
        return 1;
      }
      const auto p = parent->GetHandle();
      const auto n = new_node.GetHandle();
      LOG_F(INFO,
        "scene.create_node child: name='{}' parent_sid={} parent_idx={} "
        "child_sid={} child_idx={}",
        std::string(name, len), static_cast<unsigned>(p.GetSceneId()),
        static_cast<unsigned>(p.Index()), static_cast<unsigned>(n.GetSceneId()),
        static_cast<unsigned>(n.Index()));
    } else {
      const auto n = new_node.GetHandle();
      DLOG_F(3, "scene.create_node root: name='{}' sid={} idx={}",
        std::string(name, len), static_cast<unsigned>(n.GetSceneId()),
        static_cast<unsigned>(n.Index()));
    }
    return PushSceneNode(state, std::move(new_node));
  }

  auto LuaSceneDestroyNode(lua_State* state) -> int
  {
    if (RejectMutationOutsideScenePhase(state, "destroy_node")) {
      lua_pushboolean(state, 0);
      return 1;
    }

    auto* node = TryCheckSceneNode(state, 1);
    auto scene_ref = GetScene(state);
    if ((scene_ref == nullptr) || (node == nullptr) || !node->IsAlive()) {
      LOG_F(INFO, "scene.destroy_node rejected: scene_ref={} alive={}",
        scene_ref != nullptr, (node != nullptr) && node->IsAlive());
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto handle = node->GetHandle();
    const auto node_name = node->GetName();
    const auto* const geometry_ptr
      = static_cast<const void*>(node->GetRenderable().GetGeometry().get());
    LOG_F(INFO,
      "scene.destroy_node request: name='{}' scene_id={} node_index={} "
      "geom_ptr={}",
      node_name, static_cast<unsigned>(handle.GetSceneId()),
      static_cast<unsigned>(handle.Index()), geometry_ptr);
    const bool destroyed = scene_ref->DestroyNode(*node);
    LOG_F(INFO,
      "scene.destroy_node result: name='{}' scene_id={} node_index={} ok={}",
      node_name, static_cast<unsigned>(handle.GetSceneId()),
      static_cast<unsigned>(handle.Index()), destroyed);
    lua_pushboolean(state, destroyed ? 1 : 0);
    return 1;
  }

  auto LuaSceneDestroyHierarchy(lua_State* state) -> int
  {
    if (RejectMutationOutsideScenePhase(state, "destroy_hierarchy")) {
      lua_pushboolean(state, 0);
      return 1;
    }

    auto* node = TryCheckSceneNode(state, 1);
    auto scene_ref = GetScene(state);
    if ((scene_ref == nullptr) || (node == nullptr) || !node->IsAlive()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, scene_ref->DestroyNodeHierarchy(*node) ? 1 : 0);
    return 1;
  }

  auto LuaSceneReparent(lua_State* state) -> int
  {
    if (RejectMutationOutsideScenePhase(state, "reparent")) {
      lua_pushboolean(state, 0);
      return 1;
    }

    auto* node = TryCheckSceneNode(state, 1);
    auto scene_ref = GetScene(state);
    if ((scene_ref == nullptr) || (node == nullptr) || !node->IsAlive()
      || lua_isnil(state, 2)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* new_parent = TryCheckSceneNode(state, 2);
    if ((new_parent == nullptr) || !new_parent->IsAlive()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    bool preserve_world = true;
    if ((lua_gettop(state) >= 3) && lua_isboolean(state, 3)) {
      preserve_world = lua_toboolean(state, 3) != 0;
    }
    lua_pushboolean(state,
      scene_ref->ReparentNode(*node, *new_parent, preserve_world) ? 1 : 0);
    return 1;
  }

  auto LuaSceneRootNodes(lua_State* state) -> int
  {
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_newtable(state);
      return 1;
    }
    const auto roots = scene_ref->GetRootNodes();
    return PushSceneNodeArray(state, roots);
  }

  auto LuaSceneFindOne(lua_State* state) -> int
  {
    size_t len = 0;
    const char* path_ptr = lua_tolstring(state, 1, &len);
    if (path_ptr == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const std::string_view path(path_ptr, len);

    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    try {
      auto query = scene_ref->Query();
      if (!ApplyOptionalScope(state, query, 2)) {
        lua_pushnil(state);
        return 1;
      }
      std::optional<scene::SceneNode> result;
      const auto qr = query.FindFirstByPath(result, path);
      if (qr && result.has_value()) {
        return PushSceneNode(state, *result);
      }
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.find_one query failed: {}", e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.find_one query failed with unknown exception");
    }
    lua_pushnil(state);
    return 1;
  }

  auto LuaSceneFindMany(lua_State* state) -> int
  {
    size_t len = 0;
    const char* pattern_ptr = lua_tolstring(state, 1, &len);
    if (pattern_ptr == nullptr) {
      lua_newtable(state);
      return 1;
    }
    const std::string_view pattern(pattern_ptr, len);
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_newtable(state);
      return 1;
    }

    std::vector<scene::SceneNode> nodes;
    try {
      auto query = scene_ref->Query();
      if (!ApplyOptionalScope(state, query, 2)) {
        lua_newtable(state);
        return 1;
      }
      (void)query.CollectByPath(nodes, pattern);
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.find_many query failed: {}", e.what());
      nodes.clear();
    } catch (...) {
      LOG_F(WARNING, "scene.find_many query failed with unknown exception");
      nodes.clear();
    }
    return PushSceneNodeArray(state, nodes);
  }

  auto LuaSceneCount(lua_State* state) -> int
  {
    size_t len = 0;
    const char* pattern_ptr = lua_tolstring(state, 1, &len);
    if (pattern_ptr == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }
    const std::string_view pattern(pattern_ptr, len);
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }

    try {
      auto query = scene_ref->Query();
      if (!ApplyOptionalScope(state, query, 2)) {
        lua_pushinteger(state, 0);
        return 1;
      }
      std::vector<scene::SceneNode> nodes;
      const auto qr = query.CollectByPath(nodes, pattern);
      lua_pushinteger(state, qr ? static_cast<lua_Integer>(nodes.size()) : 0);
      return 1;
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.count query failed: {}", e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.count query failed with unknown exception");
    }
    lua_pushinteger(state, 0);
    return 1;
  }

  auto LuaSceneExists(lua_State* state) -> int
  {
    size_t len = 0;
    const char* pattern_ptr = lua_tolstring(state, 1, &len);
    if (pattern_ptr == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const std::string_view pattern(pattern_ptr, len);
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }

    try {
      auto query = scene_ref->Query();
      if (!ApplyOptionalScope(state, query, 2)) {
        lua_pushboolean(state, 0);
        return 1;
      }
      std::vector<scene::SceneNode> nodes;
      const auto qr = query.CollectByPath(nodes, pattern);
      lua_pushboolean(state, (qr && !nodes.empty()) ? 1 : 0);
      return 1;
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.exists query failed: {}", e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.exists query failed with unknown exception");
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  auto LuaSceneQuery(lua_State* state) -> int
  {
    size_t len = 0;
    const char* pattern_ptr = lua_tolstring(state, 1, &len);
    if (pattern_ptr == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneQuery(
      state, scene_ref->Query(), std::string(pattern_ptr, len));
  }

  auto LuaSceneHasEnvironment(lua_State* state) -> int
  {
    auto scene_ref = GetScene(state);
    lua_pushboolean(state,
      static_cast<int>((scene_ref != nullptr) && scene_ref->HasEnvironment()));
    return 1;
  }

  auto LuaSceneClearEnvironment(lua_State* state) -> int
  {
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    scene_ref->ClearEnvironment();
    lua_pushboolean(state, 1);
    return 1;
  }

  auto LuaSceneGetEnvironment(lua_State* state) -> int
  {
    auto scene_ref = GetScene(state);
    if ((scene_ref == nullptr) || !scene_ref->HasEnvironment()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneEnvironment(state, scene_ref);
  }

  auto LuaSceneEnsureEnvironment(lua_State* state) -> int
  {
    auto scene_ref = GetScene(state);
    if (scene_ref == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!scene_ref->HasEnvironment()) {
      scene_ref->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    }
    return PushSceneEnvironment(state, scene_ref);
  }

#ifndef NDEBUG
  auto LegacySceneMigrationTarget(const std::string_view key) -> const char*
  {
    if (key == "find") {
      return "find_one / find_many / query";
    }
    if (key == "find_path") {
      return "find_one / query";
    }
    if (key == "create") {
      return "create_node";
    }
    if (key == "current") {
      return "current_node";
    }
    if (key == "get_param") {
      return "param";
    }
    return nullptr;
  }

  auto LuaSceneLegacyIndex(lua_State* state) -> int
  {
    size_t len = 0;
    const char* key_ptr = lua_tolstring(state, 2, &len);
    if (key_ptr == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    const std::string_view key(key_ptr, len);
    const char* replacement = LegacySceneMigrationTarget(key);
    if (replacement != nullptr) {
      LOG_F(WARNING, "oxygen.scene.{} was removed in v1; use oxygen.scene.{}",
        key, replacement);
      lua_pushnil(state);
      return 1;
    }

    lua_pushnil(state);
    return 1;
  }
#endif
} // namespace

auto RegisterSceneBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  RegisterSceneNodeMetatable(state);
  RegisterSceneQueryMetatable(state);
  RegisterSceneEnvironmentMetatables(state);

  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "scene");

  lua_pushcfunction(state, LuaSceneCurrentNode, "scene.current_node");
  lua_setfield(state, module_index, "current_node");
  lua_pushcfunction(state, LuaSceneParam, "scene.param");
  lua_setfield(state, module_index, "param");

  lua_pushcfunction(state, LuaSceneCreateNode, "scene.create_node");
  lua_setfield(state, module_index, "create_node");
  lua_pushcfunction(state, LuaSceneDestroyNode, "scene.destroy_node");
  lua_setfield(state, module_index, "destroy_node");
  lua_pushcfunction(state, LuaSceneDestroyHierarchy, "scene.destroy_hierarchy");
  lua_setfield(state, module_index, "destroy_hierarchy");
  lua_pushcfunction(state, LuaSceneReparent, "scene.reparent");
  lua_setfield(state, module_index, "reparent");
  lua_pushcfunction(state, LuaSceneRootNodes, "scene.root_nodes");
  lua_setfield(state, module_index, "root_nodes");

  lua_pushcfunction(state, LuaSceneFindOne, "scene.find_one");
  lua_setfield(state, module_index, "find_one");
  lua_pushcfunction(state, LuaSceneFindMany, "scene.find_many");
  lua_setfield(state, module_index, "find_many");
  lua_pushcfunction(state, LuaSceneCount, "scene.count");
  lua_setfield(state, module_index, "count");
  lua_pushcfunction(state, LuaSceneExists, "scene.exists");
  lua_setfield(state, module_index, "exists");
  lua_pushcfunction(state, LuaSceneQuery, "scene.query");
  lua_setfield(state, module_index, "query");

  lua_pushcfunction(state, LuaSceneHasEnvironment, "scene.has_environment");
  lua_setfield(state, module_index, "has_environment");
  lua_pushcfunction(state, LuaSceneGetEnvironment, "scene.get_environment");
  lua_setfield(state, module_index, "get_environment");
  lua_pushcfunction(
    state, LuaSceneEnsureEnvironment, "scene.ensure_environment");
  lua_setfield(state, module_index, "ensure_environment");
  lua_pushcfunction(state, LuaSceneClearEnvironment, "scene.clear_environment");
  lua_setfield(state, module_index, "clear_environment");

#ifndef NDEBUG
  lua_createtable(state, 0, 1);
  lua_pushcfunction(state, LuaSceneLegacyIndex, "scene.__index");
  lua_setfield(state, -2, "__index");
  lua_setmetatable(state, module_index);
#endif

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
