//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentQueryBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto AssetsHasTexture(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(
      state, (loader != nullptr && loader->HasTexture(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetTexture(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto resource = loader->GetTexture(key);
    if (resource == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushTextureResource(state, key, std::move(resource));
  }

  auto AssetsHasBuffer(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(
      state, (loader != nullptr && loader->HasBuffer(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetBuffer(lua_State* state) -> int
  {
    const auto key = RequireResourceKey(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto resource = loader->GetBuffer(key);
    if (resource == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushBufferResource(state, key, std::move(resource));
  }

  auto AssetsHasMaterial(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(
      state, (loader != nullptr && loader->HasMaterialAsset(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetMaterial(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto asset = loader->GetMaterialAsset(key);
    if (asset == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushMaterialAsset(state, std::move(asset));
  }

  auto AssetsHasGeometry(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(
      state, (loader != nullptr && loader->HasGeometryAsset(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetGeometry(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto asset = loader->GetGeometryAsset(key);
    if (asset == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushGeometryAsset(state, std::move(asset));
  }

  auto AssetsHasScript(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(
      state, (loader != nullptr && loader->HasScriptAsset(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetScript(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto asset = loader->GetScriptAsset(key);
    if (asset == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushScriptAsset(state, std::move(asset));
  }

  auto AssetsHasInputAction(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(
      state, (loader != nullptr && loader->HasInputActionAsset(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetInputAction(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto asset = loader->GetInputActionAsset(key);
    if (asset == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushInputActionAsset(state, std::move(asset));
  }

  auto AssetsHasInputMappingContext(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    lua_pushboolean(state,
      (loader != nullptr && loader->HasInputMappingContextAsset(key)) ? 1 : 0);
    return 1;
  }

  auto AssetsGetInputMappingContext(lua_State* state) -> int
  {
    const auto key = RequireAssetGuid(state, 1);
    const auto loader = GetAssetLoader(state);
    if (loader == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto asset = loader->GetInputMappingContextAsset(key);
    if (asset == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushInputMappingContextAsset(state, std::move(asset));
  }
} // namespace

auto RegisterContentModuleQuery(lua_State* state, const int module_index)
  -> void
{
  lua_pushcfunction(state, AssetsHasTexture, "assets.has_texture");
  lua_setfield(state, module_index, "has_texture");
  lua_pushcfunction(state, AssetsGetTexture, "assets.get_texture");
  lua_setfield(state, module_index, "get_texture");
  lua_pushcfunction(state, AssetsHasBuffer, "assets.has_buffer");
  lua_setfield(state, module_index, "has_buffer");
  lua_pushcfunction(state, AssetsGetBuffer, "assets.get_buffer");
  lua_setfield(state, module_index, "get_buffer");

  lua_pushcfunction(state, AssetsHasMaterial, "assets.has_material");
  lua_setfield(state, module_index, "has_material");
  lua_pushcfunction(state, AssetsGetMaterial, "assets.get_material");
  lua_setfield(state, module_index, "get_material");
  lua_pushcfunction(state, AssetsHasGeometry, "assets.has_geometry");
  lua_setfield(state, module_index, "has_geometry");
  lua_pushcfunction(state, AssetsGetGeometry, "assets.get_geometry");
  lua_setfield(state, module_index, "get_geometry");
  lua_pushcfunction(state, AssetsHasScript, "assets.has_script");
  lua_setfield(state, module_index, "has_script");
  lua_pushcfunction(state, AssetsGetScript, "assets.get_script");
  lua_setfield(state, module_index, "get_script");
  lua_pushcfunction(state, AssetsHasInputAction, "assets.has_input_action");
  lua_setfield(state, module_index, "has_input_action");
  lua_pushcfunction(state, AssetsGetInputAction, "assets.get_input_action");
  lua_setfield(state, module_index, "get_input_action");
  lua_pushcfunction(
    state, AssetsHasInputMappingContext, "assets.has_input_mapping_context");
  lua_setfield(state, module_index, "has_input_mapping_context");
  lua_pushcfunction(
    state, AssetsGetInputMappingContext, "assets.get_input_mapping_context");
  lua_setfield(state, module_index, "get_input_mapping_context");
}

} // namespace oxygen::scripting::bindings
