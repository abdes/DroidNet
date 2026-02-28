//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <new>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>

namespace oxygen::scripting::bindings {

namespace {
} // namespace

auto PushTextureResource(lua_State* state, const content::ResourceKey key,
  std::shared_ptr<const data::TextureResource> resource) -> int
{
  void* const userdata_mem = lua_newuserdatatagged(
    state, sizeof(TextureResourceUserdata), kTagTextureResource);
  auto* userdata = new (userdata_mem) TextureResourceUserdata();
  userdata->key = key;
  userdata->resource = std::move(resource);
  luaL_getmetatable(state, kTextureResourceMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto PushBufferResource(lua_State* state, const content::ResourceKey key,
  std::shared_ptr<const data::BufferResource> resource) -> int
{
  void* const userdata_mem = lua_newuserdatatagged(
    state, sizeof(BufferResourceUserdata), kTagBufferResource);
  auto* userdata = new (userdata_mem) BufferResourceUserdata();
  userdata->key = key;
  userdata->resource = std::move(resource);
  luaL_getmetatable(state, kBufferResourceMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto PushMaterialAsset(
  lua_State* state, std::shared_ptr<const data::MaterialAsset> asset) -> int
{
  void* const userdata_mem
    = lua_newuserdatatagged(state, sizeof(AssetUserdata), kTagAsset);
  auto* userdata = new (userdata_mem) AssetUserdata();
  userdata->kind = AssetUserdataKind::kMaterial;
  userdata->material = std::move(asset);
  luaL_getmetatable(state, kMaterialAssetMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto PushGeometryAsset(
  lua_State* state, std::shared_ptr<const data::GeometryAsset> asset) -> int
{
  void* const userdata_mem
    = lua_newuserdatatagged(state, sizeof(AssetUserdata), kTagAsset);
  auto* userdata = new (userdata_mem) AssetUserdata();
  userdata->kind = AssetUserdataKind::kGeometry;
  userdata->geometry = std::move(asset);
  luaL_getmetatable(state, kGeometryAssetMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto PushScriptAsset(
  lua_State* state, std::shared_ptr<const data::ScriptAsset> asset) -> int
{
  void* const userdata_mem
    = lua_newuserdatatagged(state, sizeof(AssetUserdata), kTagAsset);
  auto* userdata = new (userdata_mem) AssetUserdata();
  userdata->kind = AssetUserdataKind::kScript;
  userdata->script = std::move(asset);
  luaL_getmetatable(state, kScriptAssetMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto PushInputActionAsset(
  lua_State* state, std::shared_ptr<const data::InputActionAsset> asset) -> int
{
  void* const userdata_mem
    = lua_newuserdatatagged(state, sizeof(AssetUserdata), kTagAsset);
  auto* userdata = new (userdata_mem) AssetUserdata();
  userdata->kind = AssetUserdataKind::kInputAction;
  userdata->input_action = std::move(asset);
  luaL_getmetatable(state, kInputActionAssetMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto PushInputMappingContextAsset(lua_State* state,
  std::shared_ptr<const data::InputMappingContextAsset> asset) -> int
{
  void* const userdata_mem
    = lua_newuserdatatagged(state, sizeof(AssetUserdata), kTagAsset);
  auto* userdata = new (userdata_mem) AssetUserdata();
  userdata->kind = AssetUserdataKind::kInputMappingContext;
  userdata->input_mapping_context = std::move(asset);
  luaL_getmetatable(state, kInputMappingContextAssetMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

auto RequireResourceKey(lua_State* state, const int arg_index)
  -> content::ResourceKey
{
  const auto raw = luaL_checkinteger(state, arg_index);
  if (raw < 0) {
    luaL_argerror(state, arg_index, "resource_key must be >= 0");
    return content::ResourceKey {};
  }
  return content::ResourceKey { static_cast<uint64_t>(raw) };
}

auto TryParseAssetGuid(const std::string_view text)
  -> std::optional<data::AssetKey>
{
  const auto parsed = Uuid::FromString(text);
  if (!parsed) {
    return std::nullopt;
  }
  return data::AssetKey { parsed.value() };
}

auto RequireAssetGuid(lua_State* state, const int arg_index) -> data::AssetKey
{
  size_t len = 0;
  const char* guid = luaL_checklstring(state, arg_index, &len);
  const auto parsed = TryParseAssetGuid(std::string_view(guid, len));
  if (!parsed.has_value()) {
    luaL_argerror(state, arg_index, "asset_guid must be canonical UUID string");
    return data::AssetKey {};
  }
  return *parsed;
}

auto GetAssetLoader(lua_State* state) noexcept
  -> observer_ptr<content::IAssetLoader>
{
  const auto engine = GetActiveEngine(state);
  if (engine == nullptr) {
    return {};
  }
  return engine->GetAssetLoader();
}

auto IsAssetLoaderEnabled(lua_State* state) -> bool
{
  const auto engine = GetActiveEngine(state);
  if (engine == nullptr) {
    return false;
  }
  return engine->GetEngineConfig().enable_asset_loader;
}

} // namespace oxygen::scripting::bindings
