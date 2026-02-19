//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentLifecycleBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto AssetsAvailable(lua_State* state) -> int
  {
    lua_pushboolean(state, GetAssetLoader(state) != nullptr ? 1 : 0);
    return 1;
  }

  auto AssetsEnabled(lua_State* state) -> int
  {
    lua_pushboolean(state, IsAssetLoaderEnabled(state) ? 1 : 0);
    return 1;
  }

  auto AssetsReleaseResource(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, loader->ReleaseResource(key) ? 1 : 0);
    return 1;
  }

  auto AssetsReleaseAsset(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, loader->ReleaseAsset(key) ? 1 : 0);
    return 1;
  }

  auto AssetsTrimCache(lua_State* state) -> int
  {
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    loader->TrimCache();
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsMintSyntheticTextureKey(lua_State* state) -> int
  {
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }
    lua_pushinteger(
      state, static_cast<lua_Integer>(loader->MintSyntheticTextureKey().get()));
    return 1;
  }

  auto AssetsMintSyntheticBufferKey(lua_State* state) -> int
  {
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }
    lua_pushinteger(
      state, static_cast<lua_Integer>(loader->MintSyntheticBufferKey().get()));
    return 1;
  }

  auto AssetsAddPakFile(lua_State* state) -> int
  {
    size_t path_len = 0;
    const char* path_raw = luaL_checklstring(state, 1, &path_len);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    loader->AddPakFile(
      std::filesystem::path(std::string_view(path_raw, path_len)));
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsAddLooseCookedRoot(lua_State* state) -> int
  {
    size_t path_len = 0;
    const char* path_raw = luaL_checklstring(state, 1, &path_len);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    loader->AddLooseCookedRoot(
      std::filesystem::path(std::string_view(path_raw, path_len)));
    lua_pushboolean(state, 1);
    return 1;
  }

  auto AssetsClearMounts(lua_State* state) -> int
  {
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    loader->ClearMounts();
    lua_pushboolean(state, 1);
    return 1;
  }

#ifndef NDEBUG
  auto LegacyAssetsMigrationTarget(const std::string_view key) -> const char*
  {
    if (key == "find" || key == "find_path") {
      return "has_* / get_* / load_*_async";
    }
    if (key == "hasMaterial") {
      return "has_material";
    }
    if (key == "getMaterial") {
      return "get_material";
    }
    return nullptr;
  }

  auto AssetsLegacyIndex(lua_State* state) -> int
  {
    size_t len = 0;
    const char* key_raw = lua_tolstring(state, 2, &len);
    if (key_raw == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const std::string_view key(key_raw, len);
    if (const char* replacement = LegacyAssetsMigrationTarget(key);
      replacement != nullptr) {
      luaL_error(state,
        "oxygen.assets.%.*s was removed in v1; use oxygen.assets.%s",
        static_cast<int>(key.size()), key.data(), replacement);
      return 0;
    }
    lua_pushnil(state);
    return 1;
  }
#endif
} // namespace

auto RegisterContentModuleAvailability(lua_State* state, const int module_index)
  -> void
{
  lua_pushcfunction(state, AssetsAvailable, "assets.available");
  lua_setfield(state, module_index, "available");
  lua_pushcfunction(state, AssetsEnabled, "assets.enabled");
  lua_setfield(state, module_index, "enabled");
}

auto RegisterContentModuleLifecycle(lua_State* state, const int module_index)
  -> void
{
  lua_pushcfunction(state, AssetsReleaseResource, "assets.release_resource");
  lua_setfield(state, module_index, "release_resource");
  lua_pushcfunction(state, AssetsReleaseAsset, "assets.release_asset");
  lua_setfield(state, module_index, "release_asset");
  lua_pushcfunction(state, AssetsTrimCache, "assets.trim_cache");
  lua_setfield(state, module_index, "trim_cache");

  lua_pushcfunction(
    state, AssetsMintSyntheticTextureKey, "assets.mint_synthetic_texture_key");
  lua_setfield(state, module_index, "mint_synthetic_texture_key");
  lua_pushcfunction(
    state, AssetsMintSyntheticBufferKey, "assets.mint_synthetic_buffer_key");
  lua_setfield(state, module_index, "mint_synthetic_buffer_key");

  lua_pushcfunction(state, AssetsAddPakFile, "assets.add_pak_file");
  lua_setfield(state, module_index, "add_pak_file");
  lua_pushcfunction(
    state, AssetsAddLooseCookedRoot, "assets.add_loose_cooked_root");
  lua_setfield(state, module_index, "add_loose_cooked_root");
  lua_pushcfunction(state, AssetsClearMounts, "assets.clear_mounts");
  lua_setfield(state, module_index, "clear_mounts");
}

auto RegisterContentModuleLegacyGuards(lua_State* state, const int module_index)
  -> void
{
#ifndef NDEBUG
  lua_createtable(state, 0, 1);
  lua_pushcfunction(state, AssetsLegacyIndex, "assets.__index");
  lua_setfield(state, -2, "__index");
  lua_setmetatable(state, module_index);
#else
  (void)state;
  (void)module_index;
#endif
}

} // namespace oxygen::scripting::bindings
