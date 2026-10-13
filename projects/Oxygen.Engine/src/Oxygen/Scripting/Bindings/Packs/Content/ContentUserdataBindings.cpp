//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <span>
#include <string>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
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

  constexpr auto kTextureResourceMethods = std::to_array<BindingFunction>({
    {
      .name = "is_valid",
      .function = TextureIsValid,
      .signature = "() -> boolean",
    },
    { .name = "key", .function = TextureKey, .signature = "() -> number" },
    {
      .name = "type_name",
      .function = TextureTypeName,
      .signature = "() -> string",
    },
    {
      .name = "to_string",
      .function = TextureToString,
      .signature = "() -> string",
    },
  });

  constexpr auto kBufferResourceMethods = std::to_array<BindingFunction>({
    {
      .name = "is_valid",
      .function = BufferIsValid,
      .signature = "() -> boolean",
    },
    { .name = "key", .function = BufferKey, .signature = "() -> number" },
    {
      .name = "type_name",
      .function = BufferTypeName,
      .signature = "() -> string",
    },
    {
      .name = "to_string",
      .function = BufferToString,
      .signature = "() -> string",
    },
  });

  constexpr auto kMaterialAssetMethods = std::to_array<BindingFunction>({
    {
      .name = "is_valid",
      .function = MaterialIsValid,
      .signature = "() -> boolean",
    },
    { .name = "guid", .function = MaterialGuid, .signature = "() -> string?" },
    {
      .name = "type_name",
      .function = MaterialTypeName,
      .signature = "() -> string",
    },
    {
      .name = "to_string",
      .function = MaterialToString,
      .signature = "() -> string",
    },
  });

  constexpr auto kGeometryAssetMethods = std::to_array<BindingFunction>({
    {
      .name = "is_valid",
      .function = GeometryIsValid,
      .signature = "() -> boolean",
    },
    { .name = "guid", .function = GeometryGuid, .signature = "() -> string?" },
    {
      .name = "type_name",
      .function = GeometryTypeName,
      .signature = "() -> string",
    },
    {
      .name = "to_string",
      .function = GeometryToString,
      .signature = "() -> string",
    },
  });

  constexpr auto kScriptAssetMethods = std::to_array<BindingFunction>({
    {
      .name = "is_valid",
      .function = ScriptIsValid,
      .signature = "() -> boolean",
    },
    { .name = "guid", .function = ScriptGuid, .signature = "() -> string?" },
    {
      .name = "type_name",
      .function = ScriptTypeName,
      .signature = "() -> string",
    },
    {
      .name = "to_string",
      .function = ScriptToString,
      .signature = "() -> string",
    },
  });

  constexpr auto kInputActionAssetMethods = std::to_array<BindingFunction>({
    {
      .name = "is_valid",
      .function = InputActionIsValid,
      .signature = "() -> boolean",
    },
    {
      .name = "guid",
      .function = InputActionGuid,
      .signature = "() -> string?",
    },
    {
      .name = "type_name",
      .function = InputActionTypeName,
      .signature = "() -> string",
    },
    {
      .name = "to_string",
      .function = InputActionToString,
      .signature = "() -> string",
    },
  });

  constexpr auto kInputMappingContextAssetMethods
    = std::to_array<BindingFunction>({
      {
        .name = "is_valid",
        .function = InputMappingContextIsValid,
        .signature = "() -> boolean",
      },
      {
        .name = "guid",
        .function = InputMappingContextGuid,
        .signature = "() -> string?",
      },
      {
        .name = "type_name",
        .function = InputMappingContextTypeName,
        .signature = "() -> string",
      },
      {
        .name = "to_string",
        .function = InputMappingContextToString,
        .signature = "() -> string",
      },
    });

  constexpr auto kContentClasses = std::to_array<BindingClass>({
    {
      .name = "TextureResource",
      .metatable = kTextureResourceMetatableName,
      .methods = kTextureResourceMethods,
    },
    {
      .name = "BufferResource",
      .metatable = kBufferResourceMetatableName,
      .methods = kBufferResourceMethods,
    },
    {
      .name = "MaterialAsset",
      .metatable = kMaterialAssetMetatableName,
      .methods = kMaterialAssetMethods,
    },
    {
      .name = "GeometryAsset",
      .metatable = kGeometryAssetMetatableName,
      .methods = kGeometryAssetMethods,
    },
    {
      .name = "ScriptAsset",
      .metatable = kScriptAssetMetatableName,
      .methods = kScriptAssetMethods,
    },
    {
      .name = "InputActionAsset",
      .metatable = kInputActionAssetMetatableName,
      .methods = kInputActionAssetMethods,
    },
    {
      .name = "InputMappingContextAsset",
      .metatable = kInputMappingContextAssetMetatableName,
      .methods = kInputMappingContextAssetMethods,
    },
  });

  //! Creates a metatable whose `__index` table holds the methods.
  auto RegisterMetatable(lua_State* state, const char* name,
    const std::span<const BindingFunction> methods,
    const lua_CFunction tostring_fn) -> void
  {
    if (luaL_newmetatable(state, name) == 0) {
      lua_pop(state, 1);
      return;
    }

    lua_newtable(state);
    RegisterFunctions(state, -1, methods);
    lua_setfield(state, -2, "__index");

    lua_pushcfunction(state, tostring_fn, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pop(state, 1);
  }

  auto TextureResourceDtor(lua_State* /*state*/, void* data) -> void
  {
    static_cast<TextureResourceUserdata*>(data)->~TextureResourceUserdata();
  }

  auto BufferResourceDtor(lua_State* /*state*/, void* data) -> void
  {
    static_cast<BufferResourceUserdata*>(data)->~BufferResourceUserdata();
  }

  auto AssetDtor(lua_State* /*state*/, void* data) -> void
  {
    static_cast<AssetUserdata*>(data)->~AssetUserdata();
  }
} // namespace

auto RegisterContentUserdataMetatables(lua_State* state) -> void
{
  lua_setuserdatadtor(state, kTagTextureResource, TextureResourceDtor);
  lua_setuserdatadtor(state, kTagBufferResource, BufferResourceDtor);
  lua_setuserdatadtor(state, kTagAsset, AssetDtor);

  RegisterMetatable(state, kTextureResourceMetatableName,
    kTextureResourceMethods, TextureToString);
  RegisterMetatable(state, kBufferResourceMetatableName, kBufferResourceMethods,
    BufferToString);
  RegisterMetatable(state, kMaterialAssetMetatableName, kMaterialAssetMethods,
    MaterialToString);
  RegisterMetatable(state, kGeometryAssetMetatableName, kGeometryAssetMethods,
    GeometryToString);
  RegisterMetatable(
    state, kScriptAssetMetatableName, kScriptAssetMethods, ScriptToString);
  RegisterMetatable(state, kInputActionAssetMetatableName,
    kInputActionAssetMethods, InputActionToString);
  RegisterMetatable(state, kInputMappingContextAssetMetatableName,
    kInputMappingContextAssetMethods, InputMappingContextToString);
}

auto ContentUserdataClasses() -> std::span<const BindingClass>
{
  return kContentClasses;
}

} // namespace oxygen::scripting::bindings
