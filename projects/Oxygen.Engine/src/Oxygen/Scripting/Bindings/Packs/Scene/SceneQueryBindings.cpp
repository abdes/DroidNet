//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneQueryBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr const char* kSceneQueryMetatable = "oxygen.scene.query";

  struct SceneQueryUserdata {
    scene::SceneQuery query;
    std::string pattern;
    std::vector<scene::SceneNode> scope;
  };

  auto TryCheckSceneQuery(lua_State* state, const int index)
    -> SceneQueryUserdata*
  {
    if (lua_type(state, index) != LUA_TUSERDATA) {
      return nullptr;
    }
    if (lua_userdatatag(state, index) != kTagSceneQuery) {
      return nullptr;
    }
    return static_cast<SceneQueryUserdata*>(lua_touserdata(state, index));
  }

  auto ApplyScope(SceneQueryUserdata& q) -> void
  {
    q.query.ResetTraversalScope();
    if (!q.scope.empty()) {
      q.query.AddToTraversalScope(
        std::span<const scene::SceneNode>(q.scope.data(), q.scope.size()));
    }
  }

  auto SceneQueryDtor(lua_State* /*state*/, void* data) -> void
  {
    static_cast<SceneQueryUserdata*>(data)->~SceneQueryUserdata();
  }

  auto SceneQueryToString(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_pushliteral(state, "SceneQuery(invalid)");
      return 1;
    }
    const auto str = std::string("SceneQuery(") + q->pattern + ")";
    lua_pushlstring(state, str.c_str(), str.size());
    return 1;
  }

  auto SceneQueryScope(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_settop(state, 1);
      return 1;
    }
    auto* node = TryCheckSceneNode(state, 2);
    if ((node != nullptr) && node->IsAlive()) {
      q->scope.push_back(*node);
    }
    lua_settop(state, 1);
    return 1;
  }

  auto SceneQueryScopeMany(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_settop(state, 1);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_settop(state, 1);
      return 1;
    }

    const auto count = lua_objlen(state, 2);
    for (int i = 1; i <= count; ++i) {
      lua_rawgeti(state, 2, i);
      if (!lua_isnil(state, -1)) {
        auto* node = TryCheckSceneNode(state, -1);
        if ((node != nullptr) && node->IsAlive()) {
          q->scope.push_back(*node);
        }
      }
      lua_pop(state, 1);
    }
    lua_settop(state, 1);
    return 1;
  }

  auto SceneQueryClearScope(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_settop(state, 1);
      return 1;
    }
    q->scope.clear();
    lua_settop(state, 1);
    return 1;
  }

  auto SceneQueryFirst(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    try {
      ApplyScope(*q);
      std::vector<scene::SceneNode> nodes;
      const auto qr = q->query.CollectByPath(nodes, q->pattern);
      if (qr && !nodes.empty()) {
        return PushSceneNode(state, nodes.front());
      }
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.query:first failed for pattern '{}': {}",
        q->pattern, e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.query:first failed for pattern '{}' (unknown)",
        q->pattern);
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneQueryAll(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_newtable(state);
      return 1;
    }
    lua_newtable(state);
    try {
      ApplyScope(*q);
      std::vector<scene::SceneNode> nodes;
      const auto qr = q->query.CollectByPath(nodes, q->pattern);
      if (!qr) {
        return 1;
      }
      int lua_index = 1;
      for (const auto& node : nodes) {
        PushSceneNode(state, node);
        lua_rawseti(state, -2, lua_index);
        ++lua_index;
      }
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.query:all failed for pattern '{}': {}", q->pattern,
        e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.query:all failed for pattern '{}' (unknown)",
        q->pattern);
    }
    return 1;
  }

  auto SceneQueryCount(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }
    try {
      ApplyScope(*q);
      std::vector<scene::SceneNode> nodes;
      const auto qr = q->query.CollectByPath(nodes, q->pattern);
      lua_pushinteger(state, qr ? static_cast<lua_Integer>(nodes.size()) : 0);
      return 1;
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.query:count failed for pattern '{}': {}",
        q->pattern, e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.query:count failed for pattern '{}' (unknown)",
        q->pattern);
    }
    lua_pushinteger(state, 0);
    return 1;
  }

  auto SceneQueryAny(lua_State* state) -> int
  {
    auto* q = TryCheckSceneQuery(state, 1);
    if (q == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    try {
      ApplyScope(*q);
      std::vector<scene::SceneNode> nodes;
      const auto qr = q->query.CollectByPath(nodes, q->pattern);
      lua_pushboolean(state, (qr && !nodes.empty()) ? 1 : 0);
      return 1;
    } catch (const std::exception& e) {
      LOG_F(WARNING, "scene.query:any failed for pattern '{}': {}", q->pattern,
        e.what());
    } catch (...) {
      LOG_F(WARNING, "scene.query:any failed for pattern '{}' (unknown)",
        q->pattern);
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  //! Scoping methods return the query, so calls chain.
  constexpr auto kSceneQueryMethods = std::to_array<BindingFunction>({
    {
      .name = "scope",
      .function = SceneQueryScope,
      .signature = "(node: SceneNode) -> SceneQuery",
    },
    {
      .name = "scope_many",
      .function = SceneQueryScopeMany,
      .signature = "(nodes: { SceneNode }) -> SceneQuery",
    },
    {
      .name = "clear_scope",
      .function = SceneQueryClearScope,
      .signature = "() -> SceneQuery",
    },
    {
      .name = "first",
      .function = SceneQueryFirst,
      .signature = "() -> SceneNode?",
    },
    {
      .name = "all",
      .function = SceneQueryAll,
      .signature = "() -> { SceneNode }",
    },
    {
      .name = "count",
      .function = SceneQueryCount,
      .signature = "() -> number",
    },
    { .name = "any", .function = SceneQueryAny, .signature = "() -> boolean" },
  });

  constexpr auto kSceneQueryClasses = std::to_array<BindingClass>({
    {
      .name = "SceneQuery",
      .metatable = kSceneQueryMetatable,
      .methods = kSceneQueryMethods,
    },
  });

  constexpr auto kSceneQueryDescription
    = std::to_array<BindingNamespaceDescription>({
      { .path = "scene", .classes = kSceneQueryClasses },
    });
} // namespace

auto RegisterSceneQueryMetatable(lua_State* state) -> void
{
  luaL_newmetatable(state, kSceneQueryMetatable);
  lua_setuserdatadtor(state, kTagSceneQuery, SceneQueryDtor);
  lua_pushcfunction(state, SceneQueryToString, "__tostring");
  lua_setfield(state, -2, "__tostring");

  lua_pushvalue(state, -1);
  lua_setfield(state, -2, "__index");

  RegisterFunctions(state, -1, kSceneQueryMethods);
  lua_pop(state, 1);
}

auto DescribeSceneQueryBindings()
  -> std::span<const BindingNamespaceDescription>
{
  return kSceneQueryDescription;
}

auto PushSceneQuery(
  lua_State* state, scene::SceneQuery query, std::string pattern) -> int
{
  void* data
    = lua_newuserdatatagged(state, sizeof(SceneQueryUserdata), kTagSceneQuery);
  new (data) SceneQueryUserdata {
    .query = std::move(query),
    .pattern = std::move(pattern),
    .scope = {},
  };

  if (luaL_getmetatable(state, kSceneQueryMetatable) != 0) {
    lua_setmetatable(state, -2);
  } else {
    lua_pop(state, 1);
    lua_pushnil(state);
    return 1;
  }
  return 1;
}

} // namespace oxygen::scripting::bindings
