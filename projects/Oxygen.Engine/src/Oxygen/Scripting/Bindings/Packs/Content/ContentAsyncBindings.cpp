//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentAsyncBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>

namespace oxygen::scripting::bindings {

namespace {
  auto CallLuaResourceCallback(lua_State* state, const int callback_ref,
    const content::ResourceKey key,
    std::shared_ptr<const data::TextureResource> texture,
    std::shared_ptr<const data::BufferResource> buffer) -> void
  {
    lua_getref(state, callback_ref);
    if (lua_isfunction(state, -1) == 0) {
      lua_pop(state, 1);
      LOG_F(ERROR, "oxygen.assets callback ref is not callable");
      return;
    }

    if (texture != nullptr) {
      (void)PushTextureResource(state, key, std::move(texture));
    } else if (buffer != nullptr) {
      (void)PushBufferResource(state, key, std::move(buffer));
    } else {
      lua_pushnil(state);
    }

    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
      const auto* err = lua_tostring(state, -1);
      LOG_F(ERROR, "oxygen.assets callback failed: {}",
        err == nullptr ? "unknown error" : err);
      lua_pop(state, 1);
    }
  }

  auto CallLuaAssetCallback(lua_State* state, const int callback_ref,
    std::shared_ptr<const data::MaterialAsset> material,
    std::shared_ptr<const data::GeometryAsset> geometry,
    std::shared_ptr<const data::ScriptAsset> script,
    std::shared_ptr<const data::InputActionAsset> input_action,
    std::shared_ptr<const data::InputMappingContextAsset> input_mapping_context)
    -> void
  {
    lua_getref(state, callback_ref);
    if (lua_isfunction(state, -1) == 0) {
      lua_pop(state, 1);
      LOG_F(ERROR, "oxygen.assets callback ref is not callable");
      return;
    }

    if (material != nullptr) {
      (void)PushMaterialAsset(state, std::move(material));
    } else if (geometry != nullptr) {
      (void)PushGeometryAsset(state, std::move(geometry));
    } else if (script != nullptr) {
      (void)PushScriptAsset(state, std::move(script));
    } else if (input_action != nullptr) {
      (void)PushInputActionAsset(state, std::move(input_action));
    } else if (input_mapping_context != nullptr) {
      (void)PushInputMappingContextAsset(
        state, std::move(input_mapping_context));
    } else {
      lua_pushnil(state);
    }

    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
      const auto* err = lua_tostring(state, -1);
      LOG_F(ERROR, "oxygen.assets callback failed: {}",
        err == nullptr ? "unknown error" : err);
      lua_pop(state, 1);
    }
  }

  auto RequireCallbackRef(lua_State* state, const int arg_index) -> int
  {
    luaL_checktype(state, arg_index, LUA_TFUNCTION);
    lua_pushvalue(state, arg_index);
    return lua_ref(state, -1);
  }

  auto AssetsLoadTextureAsync(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    loader->StartLoadTexture(key, [state, callback_ref, key](auto resource) {
      CallLuaResourceCallback(
        state, callback_ref, key, std::move(resource), {});
      lua_unref(state, callback_ref);
    });
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsLoadBufferAsync(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    loader->StartLoadBuffer(key, [state, callback_ref, key](auto resource) {
      CallLuaResourceCallback(
        state, callback_ref, key, {}, std::move(resource));
      lua_unref(state, callback_ref);
    });
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsLoadMaterialAsync(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    loader->StartLoadMaterialAsset(key, [state, callback_ref](auto asset) {
      CallLuaAssetCallback(
        state, callback_ref, std::move(asset), {}, {}, {}, {});
      lua_unref(state, callback_ref);
    });
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsLoadGeometryAsync(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    loader->StartLoadGeometryAsset(key, [state, callback_ref](auto asset) {
      CallLuaAssetCallback(
        state, callback_ref, {}, std::move(asset), {}, {}, {});
      lua_unref(state, callback_ref);
    });
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsLoadScriptAsync(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    loader->StartLoadScriptAsset(key, [state, callback_ref](auto asset) {
      CallLuaAssetCallback(
        state, callback_ref, {}, {}, std::move(asset), {}, {});
      lua_unref(state, callback_ref);
    });
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsLoadInputActionAsync(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    // HOOK-BACKLOG(content.v1): IAssetLoader does not currently expose
    // StartLoadInputActionAsset. v1 preserves API shape and provides a
    // deterministic cache-resolve callback path until loader hooks are added.
    LOG_F(WARNING,
      "oxygen.assets.load_input_action_async uses cache-only fallback (loader "
      "hook missing)");
    auto asset = loader->GetInputActionAsset(key);
    CallLuaAssetCallback(state, callback_ref, {}, {}, {}, std::move(asset), {});
    lua_unref(state, callback_ref);
    lua_pushboolean(state, 0);
    return 1;
  }

  auto AssetsLoadInputMappingContextAsync(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const int callback_ref = RequireCallbackRef(state, 2);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_unref(state, callback_ref);
      lua_pushboolean(state, 0);
      return 1;
    }

    // HOOK-BACKLOG(content.v1): IAssetLoader does not currently expose
    // StartLoadInputMappingContextAsset. v1 preserves API shape and provides a
    // deterministic cache-resolve callback path until loader hooks are added.
    LOG_F(WARNING,
      "oxygen.assets.load_input_mapping_context_async uses cache-only fallback "
      "(loader hook missing)");
    auto asset = loader->GetInputMappingContextAsset(key);
    CallLuaAssetCallback(state, callback_ref, {}, {}, {}, {}, std::move(asset));
    lua_unref(state, callback_ref);
    lua_pushboolean(state, 0);
    return 1;
  }
} // namespace

auto RegisterContentModuleAsync(lua_State* state, const int module_index)
  -> void
{
  lua_pushcfunction(state, AssetsLoadTextureAsync, "assets.load_texture_async");
  lua_setfield(state, module_index, "load_texture_async");
  lua_pushcfunction(state, AssetsLoadBufferAsync, "assets.load_buffer_async");
  lua_setfield(state, module_index, "load_buffer_async");
  lua_pushcfunction(
    state, AssetsLoadMaterialAsync, "assets.load_material_async");
  lua_setfield(state, module_index, "load_material_async");
  lua_pushcfunction(
    state, AssetsLoadGeometryAsync, "assets.load_geometry_async");
  lua_setfield(state, module_index, "load_geometry_async");
  lua_pushcfunction(state, AssetsLoadScriptAsync, "assets.load_script_async");
  lua_setfield(state, module_index, "load_script_async");
  lua_pushcfunction(
    state, AssetsLoadInputActionAsync, "assets.load_input_action_async");
  lua_setfield(state, module_index, "load_input_action_async");
  lua_pushcfunction(state, AssetsLoadInputMappingContextAsync,
    "assets.load_input_mapping_context_async");
  lua_setfield(state, module_index, "load_input_mapping_context_async");
}

} // namespace oxygen::scripting::bindings
