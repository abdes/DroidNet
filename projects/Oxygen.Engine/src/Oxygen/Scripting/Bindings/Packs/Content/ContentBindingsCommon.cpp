//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <new>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kBaseTen = 10;
  constexpr size_t kUuidStringLength = 36;
  constexpr size_t kUuidByteCount = 16;
  constexpr size_t kDashIndex0 = 8;
  constexpr size_t kDashIndex1 = 13;
  constexpr size_t kDashIndex2 = 18;
  constexpr size_t kDashIndex3 = 23;

  auto HexValue(const char c) -> int
  {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return kBaseTen + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return kBaseTen + (c - 'A');
    }
    return -1;
  }
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
  if (text.size() != kUuidStringLength) {
    return std::nullopt;
  }
  if (text[kDashIndex0] != '-' || text[kDashIndex1] != '-'
    || text[kDashIndex2] != '-' || text[kDashIndex3] != '-') {
    return std::nullopt;
  }

  std::array<uint8_t, kUuidByteCount> bytes {};
  size_t byte_index = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '-') {
      continue;
    }
    if (i + 1 >= text.size()) {
      return std::nullopt;
    }
    const int hi = HexValue(text[i]);
    const int lo = HexValue(text[i + 1]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    if (byte_index >= bytes.size()) {
      return std::nullopt;
    }
    bytes[byte_index] = static_cast<uint8_t>((hi << 4) | lo);
    ++byte_index;
    ++i;
  }

  if (byte_index != bytes.size()) {
    return std::nullopt;
  }

  return data::AssetKey { .guid = bytes };
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
