//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentUserdataBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto CheckTextureResourceUserdata(lua_State* state, const int arg_index)
    -> TextureResourceUserdata*
  {
    return static_cast<TextureResourceUserdata*>(
      luaL_checkudata(state, arg_index, kTextureResourceMetatableName));
  }

  auto CheckBufferResourceUserdata(lua_State* state, const int arg_index)
    -> BufferResourceUserdata*
  {
    return static_cast<BufferResourceUserdata*>(
      luaL_checkudata(state, arg_index, kBufferResourceMetatableName));
  }

  auto CheckAssetUserdata(lua_State* state, const int arg_index,
    const char* metatable_name) -> AssetUserdata*
  {
    return static_cast<AssetUserdata*>(
      luaL_checkudata(state, arg_index, metatable_name));
  }

  auto ResourceToString(const char* type_name, const content::ResourceKey key)
    -> std::string
  {
    return std::string(type_name)
      .append("(")
      .append(content::to_string(key))
      .append(")");
  }

  auto AssetToString(const char* type_name, const data::AssetKey key)
    -> std::string
  {
    return std::string(type_name)
      .append("(")
      .append(data::to_string(key))
      .append(")");
  }

  auto TextureIsValid(lua_State* state) -> int
  {
    const auto* userdata = CheckTextureResourceUserdata(state, 1);
    lua_pushboolean(state, userdata->resource != nullptr ? 1 : 0);
    return 1;
  }

  auto TextureKey(lua_State* state) -> int
  {
    const auto* userdata = CheckTextureResourceUserdata(state, 1);
    lua_pushinteger(state, static_cast<lua_Integer>(userdata->key.get()));
    return 1;
  }

  auto TextureTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "TextureResource");
    return 1;
  }

  auto TextureToString(lua_State* state) -> int
  {
    const auto* userdata = CheckTextureResourceUserdata(state, 1);
    const auto text = ResourceToString("TextureResource", userdata->key);
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto BufferIsValid(lua_State* state) -> int
  {
    const auto* userdata = CheckBufferResourceUserdata(state, 1);
    lua_pushboolean(state, userdata->resource != nullptr ? 1 : 0);
    return 1;
  }

  auto BufferKey(lua_State* state) -> int
  {
    const auto* userdata = CheckBufferResourceUserdata(state, 1);
    lua_pushinteger(state, static_cast<lua_Integer>(userdata->key.get()));
    return 1;
  }

  auto BufferTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "BufferResource");
    return 1;
  }

  auto BufferToString(lua_State* state) -> int
  {
    const auto* userdata = CheckBufferResourceUserdata(state, 1);
    const auto text = ResourceToString("BufferResource", userdata->key);
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto MaterialIsValid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kMaterialAssetMetatableName);
    lua_pushboolean(state, userdata->material != nullptr ? 1 : 0);
    return 1;
  }

  auto MaterialGuid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kMaterialAssetMetatableName);
    if (userdata->material == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto guid = data::to_string(userdata->material->GetAssetKey());
    lua_pushlstring(state, guid.c_str(), guid.size());
    return 1;
  }

  auto MaterialTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "MaterialAsset");
    return 1;
  }

  auto MaterialToString(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kMaterialAssetMetatableName);
    if (userdata->material == nullptr) {
      lua_pushliteral(state, "MaterialAsset(nil)");
      return 1;
    }
    const auto text
      = AssetToString("MaterialAsset", userdata->material->GetAssetKey());
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto GeometryIsValid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kGeometryAssetMetatableName);
    lua_pushboolean(state, userdata->geometry != nullptr ? 1 : 0);
    return 1;
  }

  auto GeometryGuid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kGeometryAssetMetatableName);
    if (userdata->geometry == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto guid = data::to_string(userdata->geometry->GetAssetKey());
    lua_pushlstring(state, guid.c_str(), guid.size());
    return 1;
  }

  auto GeometryTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "GeometryAsset");
    return 1;
  }

  auto GeometryToString(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kGeometryAssetMetatableName);
    if (userdata->geometry == nullptr) {
      lua_pushliteral(state, "GeometryAsset(nil)");
      return 1;
    }
    const auto text
      = AssetToString("GeometryAsset", userdata->geometry->GetAssetKey());
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto ScriptIsValid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kScriptAssetMetatableName);
    lua_pushboolean(state, userdata->script != nullptr ? 1 : 0);
    return 1;
  }

  auto ScriptGuid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kScriptAssetMetatableName);
    if (userdata->script == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto guid = data::to_string(userdata->script->GetAssetKey());
    lua_pushlstring(state, guid.c_str(), guid.size());
    return 1;
  }

  auto ScriptTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "ScriptAsset");
    return 1;
  }

  auto ScriptToString(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kScriptAssetMetatableName);
    if (userdata->script == nullptr) {
      lua_pushliteral(state, "ScriptAsset(nil)");
      return 1;
    }
    const auto text
      = AssetToString("ScriptAsset", userdata->script->GetAssetKey());
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto InputActionIsValid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kInputActionAssetMetatableName);
    lua_pushboolean(state, userdata->input_action != nullptr ? 1 : 0);
    return 1;
  }

  auto InputActionGuid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kInputActionAssetMetatableName);
    if (userdata->input_action == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto guid = data::to_string(userdata->input_action->GetAssetKey());
    lua_pushlstring(state, guid.c_str(), guid.size());
    return 1;
  }

  auto InputActionTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "InputActionAsset");
    return 1;
  }

  auto InputActionToString(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kInputActionAssetMetatableName);
    if (userdata->input_action == nullptr) {
      lua_pushliteral(state, "InputActionAsset(nil)");
      return 1;
    }
    const auto text = AssetToString(
      "InputActionAsset", userdata->input_action->GetAssetKey());
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto InputMappingContextIsValid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kInputMappingContextAssetMetatableName);
    lua_pushboolean(state, userdata->input_mapping_context != nullptr ? 1 : 0);
    return 1;
  }

  auto InputMappingContextGuid(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kInputMappingContextAssetMetatableName);
    if (userdata->input_mapping_context == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto guid
      = data::to_string(userdata->input_mapping_context->GetAssetKey());
    lua_pushlstring(state, guid.c_str(), guid.size());
    return 1;
  }

  auto InputMappingContextTypeName(lua_State* state) -> int
  {
    lua_pushliteral(state, "InputMappingContextAsset");
    return 1;
  }

  auto InputMappingContextToString(lua_State* state) -> int
  {
    const auto* userdata
      = CheckAssetUserdata(state, 1, kInputMappingContextAssetMetatableName);
    if (userdata->input_mapping_context == nullptr) {
      lua_pushliteral(state, "InputMappingContextAsset(nil)");
      return 1;
    }
    const auto text = AssetToString("InputMappingContextAsset",
      userdata->input_mapping_context->GetAssetKey());
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto RegisterMetatable(lua_State* state, const char* name,
    const lua_CFunction is_valid, const lua_CFunction id_fn,
    const lua_CFunction type_name_fn, const lua_CFunction tostring_fn) -> void
  {
    if (luaL_newmetatable(state, name) == 0) {
      lua_pop(state, 1);
      return;
    }

    lua_newtable(state);
    lua_pushcfunction(state, is_valid, "assets.userdata.is_valid");
    lua_setfield(state, -2, "is_valid");
    lua_pushcfunction(state, id_fn, "assets.userdata.id");
    lua_setfield(state, -2, "guid");
    lua_pushcfunction(state, type_name_fn, "assets.userdata.type_name");
    lua_setfield(state, -2, "type_name");
    lua_pushcfunction(state, tostring_fn, "assets.userdata.to_string");
    lua_setfield(state, -2, "to_string");
    lua_setfield(state, -2, "__index");

    lua_pushcfunction(state, tostring_fn, "assets.userdata.__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pop(state, 1);
  }

  auto RegisterResourceMetatable(lua_State* state, const char* name,
    const lua_CFunction is_valid, const lua_CFunction key_fn,
    const lua_CFunction type_name_fn, const lua_CFunction tostring_fn) -> void
  {
    if (luaL_newmetatable(state, name) == 0) {
      lua_pop(state, 1);
      return;
    }

    lua_newtable(state);
    lua_pushcfunction(state, is_valid, "assets.resource.is_valid");
    lua_setfield(state, -2, "is_valid");
    lua_pushcfunction(state, key_fn, "assets.resource.key");
    lua_setfield(state, -2, "key");
    lua_pushcfunction(state, type_name_fn, "assets.resource.type_name");
    lua_setfield(state, -2, "type_name");
    lua_pushcfunction(state, tostring_fn, "assets.resource.to_string");
    lua_setfield(state, -2, "to_string");
    lua_setfield(state, -2, "__index");

    lua_pushcfunction(state, tostring_fn, "assets.resource.__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pop(state, 1);
  }

  auto TextureGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<TextureResourceUserdata*>(
      luaL_checkudata(state, 1, kTextureResourceMetatableName));
    userdata->~TextureResourceUserdata();
    return 0;
  }

  auto BufferGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<BufferResourceUserdata*>(
      luaL_checkudata(state, 1, kBufferResourceMetatableName));
    userdata->~BufferResourceUserdata();
    return 0;
  }

  auto MaterialGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<AssetUserdata*>(
      luaL_checkudata(state, 1, kMaterialAssetMetatableName));
    userdata->~AssetUserdata();
    return 0;
  }

  auto GeometryGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<AssetUserdata*>(
      luaL_checkudata(state, 1, kGeometryAssetMetatableName));
    userdata->~AssetUserdata();
    return 0;
  }

  auto ScriptGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<AssetUserdata*>(
      luaL_checkudata(state, 1, kScriptAssetMetatableName));
    userdata->~AssetUserdata();
    return 0;
  }

  auto InputActionGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<AssetUserdata*>(
      luaL_checkudata(state, 1, kInputActionAssetMetatableName));
    userdata->~AssetUserdata();
    return 0;
  }

  auto InputMappingContextGc(lua_State* state) -> int
  {
    auto* userdata = static_cast<AssetUserdata*>(
      luaL_checkudata(state, 1, kInputMappingContextAssetMetatableName));
    userdata->~AssetUserdata();
    return 0;
  }
} // namespace

auto RegisterContentUserdataMetatables(lua_State* state) -> void
{
  RegisterResourceMetatable(state, kTextureResourceMetatableName,
    TextureIsValid, TextureKey, TextureTypeName, TextureToString);
  luaL_getmetatable(state, kTextureResourceMetatableName);
  lua_pushcfunction(state, TextureGc, "assets.texture_resource.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
  RegisterResourceMetatable(state, kBufferResourceMetatableName, BufferIsValid,
    BufferKey, BufferTypeName, BufferToString);
  luaL_getmetatable(state, kBufferResourceMetatableName);
  lua_pushcfunction(state, BufferGc, "assets.buffer_resource.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);

  RegisterMetatable(state, kMaterialAssetMetatableName, MaterialIsValid,
    MaterialGuid, MaterialTypeName, MaterialToString);
  luaL_getmetatable(state, kMaterialAssetMetatableName);
  lua_pushcfunction(state, MaterialGc, "assets.material_asset.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
  RegisterMetatable(state, kGeometryAssetMetatableName, GeometryIsValid,
    GeometryGuid, GeometryTypeName, GeometryToString);
  luaL_getmetatable(state, kGeometryAssetMetatableName);
  lua_pushcfunction(state, GeometryGc, "assets.geometry_asset.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
  RegisterMetatable(state, kScriptAssetMetatableName, ScriptIsValid, ScriptGuid,
    ScriptTypeName, ScriptToString);
  luaL_getmetatable(state, kScriptAssetMetatableName);
  lua_pushcfunction(state, ScriptGc, "assets.script_asset.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
  RegisterMetatable(state, kInputActionAssetMetatableName, InputActionIsValid,
    InputActionGuid, InputActionTypeName, InputActionToString);
  luaL_getmetatable(state, kInputActionAssetMetatableName);
  lua_pushcfunction(state, InputActionGc, "assets.input_action_asset.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
  RegisterMetatable(state, kInputMappingContextAssetMetatableName,
    InputMappingContextIsValid, InputMappingContextGuid,
    InputMappingContextTypeName, InputMappingContextToString);
  luaL_getmetatable(state, kInputMappingContextAssetMetatableName);
  lua_pushcfunction(
    state, InputMappingContextGc, "assets.input_mapping_context_asset.__gc");
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
