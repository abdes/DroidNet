//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
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
    }
    lua_pushnil(state);
    return 1;
  }
#endif

  constexpr auto kAvailabilityFunctions = std::to_array<BindingFunction>({
    {
      .name = "available",
      .function = AssetsAvailable,
      .signature = "() -> boolean",
    },
    {
      .name = "enabled",
      .function = AssetsEnabled,
      .signature = "() -> boolean",
    },
  });

  constexpr auto kLifecycleFunctions = std::to_array<BindingFunction>({
    {
      .name = "trim_cache",
      .function = AssetsTrimCache,
      .signature = "() -> boolean",
    },
    {
      .name = "mint_synthetic_texture_key",
      .function = AssetsMintSyntheticTextureKey,
      .signature = "() -> number",
    },
    {
      .name = "mint_synthetic_buffer_key",
      .function = AssetsMintSyntheticBufferKey,
      .signature = "() -> number",
    },
    {
      .name = "add_pak_file",
      .function = AssetsAddPakFile,
      .signature = "(path: string) -> boolean",
    },
    {
      .name = "add_loose_cooked_root",
      .function = AssetsAddLooseCookedRoot,
      .signature = "(path: string) -> boolean",
    },
    {
      .name = "clear_mounts",
      .function = AssetsClearMounts,
      .signature = "() -> boolean",
    },
  });
} // namespace

auto RegisterContentModuleAvailability(lua_State* state, const int module_index)
  -> void
{
  RegisterFunctions(state, module_index, kAvailabilityFunctions);
}

auto RegisterContentModuleLifecycle(lua_State* state, const int module_index)
  -> void
{
  RegisterFunctions(state, module_index, kLifecycleFunctions);
}

auto ContentAvailabilityFunctions() -> std::span<const BindingFunction>
{
  return kAvailabilityFunctions;
}

auto ContentLifecycleFunctions() -> std::span<const BindingFunction>
{
  return kLifecycleFunctions;
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
