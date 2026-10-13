//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <span>
#include <utility>

#include <lua.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
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

  constexpr auto kQueryFunctions = std::to_array<BindingFunction>({
    {
      .name = "has_texture",
      .function = AssetsHasTexture,
      .signature = "(key: number) -> boolean",
    },
    {
      .name = "get_texture",
      .function = AssetsGetTexture,
      .signature = "(key: number) -> TextureResource?",
    },
    {
      .name = "has_buffer",
      .function = AssetsHasBuffer,
      .signature = "(key: number) -> boolean",
    },
    {
      .name = "get_buffer",
      .function = AssetsGetBuffer,
      .signature = "(key: number) -> BufferResource?",
    },
    {
      .name = "has_material",
      .function = AssetsHasMaterial,
      .signature = "(guid: string) -> boolean",
    },
    {
      .name = "get_material",
      .function = AssetsGetMaterial,
      .signature = "(guid: string) -> MaterialAsset?",
    },
    {
      .name = "has_geometry",
      .function = AssetsHasGeometry,
      .signature = "(guid: string) -> boolean",
    },
    {
      .name = "get_geometry",
      .function = AssetsGetGeometry,
      .signature = "(guid: string) -> GeometryAsset?",
    },
    {
      .name = "has_script",
      .function = AssetsHasScript,
      .signature = "(guid: string) -> boolean",
    },
    {
      .name = "get_script",
      .function = AssetsGetScript,
      .signature = "(guid: string) -> ScriptAsset?",
    },
    {
      .name = "has_input_action",
      .function = AssetsHasInputAction,
      .signature = "(guid: string) -> boolean",
    },
    {
      .name = "get_input_action",
      .function = AssetsGetInputAction,
      .signature = "(guid: string) -> InputActionAsset?",
    },
    {
      .name = "has_input_mapping_context",
      .function = AssetsHasInputMappingContext,
      .signature = "(guid: string) -> boolean",
    },
    {
      .name = "get_input_mapping_context",
      .function = AssetsGetInputMappingContext,
      .signature = "(guid: string) -> InputMappingContextAsset?",
    },
  });
} // namespace

auto RegisterContentModuleQuery(lua_State* state, const int module_index)
  -> void
{
  RegisterFunctions(state, module_index, kQueryFunctions);
}

auto ContentQueryFunctions() -> std::span<const BindingFunction>
{
  return kQueryFunctions;
}

} // namespace oxygen::scripting::bindings
