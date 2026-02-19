//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/TextureResource.h>

struct lua_State;
namespace oxygen::content {
class IAssetLoader;
}

namespace oxygen::scripting::bindings {

constexpr const char* kTextureResourceMetatableName
  = "oxygen.assets.texture_resource";
constexpr const char* kBufferResourceMetatableName
  = "oxygen.assets.buffer_resource";
constexpr const char* kMaterialAssetMetatableName
  = "oxygen.assets.material_asset";
constexpr const char* kGeometryAssetMetatableName
  = "oxygen.assets.geometry_asset";
constexpr const char* kScriptAssetMetatableName = "oxygen.assets.script_asset";
constexpr const char* kInputActionAssetMetatableName
  = "oxygen.assets.input_action_asset";
constexpr const char* kInputMappingContextAssetMetatableName
  = "oxygen.assets.input_mapping_context_asset";

enum class AssetUserdataKind : int {
  kMaterial = 0,
  kGeometry = 1,
  kScript = 2,
  kInputAction = 3,
  kInputMappingContext = 4,
};

enum class ResourceUserdataKind : int {
  kTexture = 0,
  kBuffer = 1,
};

struct TextureResourceUserdata {
  content::ResourceKey key {};
  std::shared_ptr<const data::TextureResource> resource {};
};

struct BufferResourceUserdata {
  content::ResourceKey key {};
  std::shared_ptr<const data::BufferResource> resource {};
};

struct AssetUserdata {
  AssetUserdataKind kind { AssetUserdataKind::kMaterial };
  std::shared_ptr<const data::MaterialAsset> material {};
  std::shared_ptr<const data::GeometryAsset> geometry {};
  std::shared_ptr<const data::ScriptAsset> script {};
  std::shared_ptr<const data::InputActionAsset> input_action {};
  std::shared_ptr<const data::InputMappingContextAsset>
    input_mapping_context {};
};

auto PushTextureResource(lua_State* state, content::ResourceKey key,
  std::shared_ptr<const data::TextureResource> resource) -> int;
auto PushBufferResource(lua_State* state, content::ResourceKey key,
  std::shared_ptr<const data::BufferResource> resource) -> int;

auto PushMaterialAsset(
  lua_State* state, std::shared_ptr<const data::MaterialAsset> asset) -> int;
auto PushGeometryAsset(
  lua_State* state, std::shared_ptr<const data::GeometryAsset> asset) -> int;
auto PushScriptAsset(
  lua_State* state, std::shared_ptr<const data::ScriptAsset> asset) -> int;
auto PushInputActionAsset(
  lua_State* state, std::shared_ptr<const data::InputActionAsset> asset) -> int;
auto PushInputMappingContextAsset(lua_State* state,
  std::shared_ptr<const data::InputMappingContextAsset> asset) -> int;

auto RequireResourceKey(lua_State* state, int arg_index)
  -> content::ResourceKey;
auto RequireAssetGuid(lua_State* state, int arg_index) -> data::AssetKey;
auto TryParseAssetGuid(std::string_view text) -> std::optional<data::AssetKey>;

auto GetAssetLoader(lua_State* state) noexcept
  -> observer_ptr<content::IAssetLoader>;
auto IsAssetLoaderEnabled(lua_State* state) -> bool;

auto RegisterContentUserdataMetatables(lua_State* state) -> void;
auto RegisterContentModuleAvailability(lua_State* state, int module_index)
  -> void;
auto RegisterContentModuleQuery(lua_State* state, int module_index) -> void;
auto RegisterContentModuleAsync(lua_State* state, int module_index) -> void;
auto RegisterContentModuleLifecycle(lua_State* state, int module_index) -> void;
auto RegisterContentModuleProcedural(lua_State* state, int module_index)
  -> void;
auto RegisterContentModuleLegacyGuards(lua_State* state, int module_index)
  -> void;

} // namespace oxygen::scripting::bindings
