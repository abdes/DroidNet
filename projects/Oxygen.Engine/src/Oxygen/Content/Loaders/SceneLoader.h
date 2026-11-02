//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::loaders {

namespace detail {

  //! Texture bindings the scene's environment records use.
  inline auto EnvironmentTextureUses(const data::SceneAsset& asset)
    -> std::vector<data::ResourceReferenceUse>
  {
    auto uses = std::vector<data::ResourceReferenceUse> {};
    const auto add
      = [&uses](const data::ResourceReferenceIndex reference) -> void {
      if (reference != data::kNoResourceReference) {
        uses.push_back({
          .reference = reference,
          .kind = data::ResourceKind::kTexture,
        });
      }
    };
    if (const auto post = asset.TryGetPostProcessVolumeEnvironment()) {
      add(post->auto_exposure_metering_mask);
    }
    if (const auto fog = asset.TryGetFogEnvironment()) {
      add(fog->inscattering_color_cubemap);
    }
    if (const auto sky_light = asset.TryGetSkyLightEnvironment()) {
      add(sky_light->cubemap);
    }
    if (const auto sky_sphere = asset.TryGetSkySphereEnvironment()) {
      add(sky_sphere->cubemap);
    }
    return uses;
  }

  //! The assets an admitted scene names, by the type each must have.
  struct SceneAssetReferences final {
    //! What the scene's load depends on.
    std::unordered_set<data::AssetKey> geometries;
    std::unordered_map<data::AssetKey, data::AssetType> override_assets;
    std::unordered_set<data::AssetKey> scripts;
    std::unordered_map<data::AssetKey, data::AssetType> parameter_assets;
    //! What instantiation loads: a missing one is an issue on its instance or
    //! context rather than a failed scene load.
    std::unordered_set<data::AssetKey> instances;
    std::unordered_set<data::AssetKey> contexts;
  };

  inline auto CollectSceneAssetReferences(const data::SceneAsset& asset)
    -> SceneAssetReferences
  {
    namespace world = data::pak::world;
    namespace scripting = data::pak::scripting;
    auto references = SceneAssetReferences {};
    for (const auto& renderable :
      asset.GetComponents<world::RenderableRecord>()) {
      references.geometries.insert(renderable.geometry_key);
    }
    for (const auto& change : asset.GetComponents<world::OverrideRecord>()) {
      // The scene admits only overrides of properties the schema declares.
      const auto* property
        = data::schema::FindOverrideProperty(change.override_id);
      if (property->type == data::schema::ValueType::kAsset
        && !change.value.as_asset.IsNil()) {
        DCHECK_F(property->asset_type != data::AssetType::kUnknown);
        references.override_assets.insert_or_assign(
          change.value.as_asset, property->asset_type);
      }
    }
    const auto add_parameter
      = [&references](const scripting::ScriptParamRecord& param) -> void {
      if (param.type == scripting::ScriptParamType::kAsset
        && !param.value.as_asset.key.IsNil()) {
        references.parameter_assets.insert_or_assign(
          param.value.as_asset.key, param.value.as_asset.asset_type);
      }
    };
    for (const auto& change :
      asset.GetComponents<scripting::ScriptParamOverrideRecord>()) {
      add_parameter(change.param);
    }
    for (const auto& component :
      asset.GetComponents<scripting::ScriptingComponentRecord>()) {
      for (const auto& slot : asset.ReadScriptSlots(
             component.slot_start_index, component.slot_count)) {
        references.scripts.insert(slot.script_asset_key);
        for (const auto& param : asset.ReadScriptParameters(slot)) {
          add_parameter(param);
        }
      }
    }
    for (const auto& instance :
      asset.GetComponents<world::SceneInstanceRecord>()) {
      references.instances.insert(instance.scene_key);
    }
    for (const auto& context :
      asset.GetComponents<world::StartingInputContextRecord>()) {
      references.contexts.insert(context.context_key);
    }
    return references;
  }

} // namespace detail

//! Loader for scene assets.
/*!
 Reads the descriptor the context's reader holds, from its position to its
 end, and admits it as a `data::SceneAsset`, which refuses a malformed scene
 with a `data::CookedOpenError`. The loader then checks what the scene
 references and registers what its load depends on.
*/
inline auto LoadSceneAsset(const LoaderContext& context)
  -> std::unique_ptr<data::SceneAsset>
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(2, "offline mode   : {}", context.work_offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;
  auto bytes_res = reader.ReadBlob(reader.Remaining().get());
  CheckLoaderResult(bytes_res, "scene asset", "ReadBlob(descriptor)");

  auto asset = std::make_unique<data::SceneAsset>(context.current_asset_key,
    std::move(*bytes_res),
    data::SourceOrigin {
      .key = context.source_key,
      .instance = context.source_instance,
    });
  const auto references = detail::CollectSceneAssetReferences(*asset);

  if (!context.parse_only || context.dependency_collector) {
    if (!context.dependency_collector) {
      LOG_F(ERROR,
        "SceneLoader requires a DependencyCollector for non-parse-only loads");
      throw std::runtime_error(
        "SceneLoader requires a DependencyCollector for async decode");
    }
    auto& collector = *context.dependency_collector;
    for (const auto& dep : references.geometries) {
      collector.AddAssetDependency(dep);
    }
    for (const auto& dep : references.override_assets | std::views::keys) {
      collector.AddAssetDependency(dep);
    }
    for (const auto& dep : references.scripts) {
      collector.AddAssetDependency(dep);
    }
    // Changes of instanced slots name assets whether or not the scene has
    // scripts of its own.
    for (const auto& dep : references.parameter_assets | std::views::keys) {
      collector.AddAssetDependency(dep);
    }
  }

  if (context.asset_references) {
    std::vector<data::KeyReference> keys;
    const auto add_asset
      = [&keys](const data::AssetKey& key, const data::AssetType type) -> void {
      keys.push_back({
        .key = key,
        .kind = data::KeyReferenceKind::kAsset,
        .expected_type = type,
      });
    };
    for (const auto& key : references.geometries) {
      add_asset(key, data::AssetType::kGeometry);
    }
    for (const auto& [key, type] : references.override_assets) {
      add_asset(key, type);
    }
    for (const auto& key : references.scripts) {
      add_asset(key, data::AssetType::kScript);
    }
    for (const auto& [key, type] : references.parameter_assets) {
      add_asset(key, type);
    }
    for (const auto& key : references.instances) {
      add_asset(key, data::AssetType::kScene);
    }
    for (const auto& key : references.contexts) {
      add_asset(key, data::AssetType::kInputMappingContext);
    }
    context.ValidateReferences(detail::EnvironmentTextureUses(*asset), keys);
  }

  if (!context.parse_only) {
    for (const auto& use : detail::EnvironmentTextureUses(*asset)) {
      const auto index = context.ResolveResource(use.reference, use.kind);
      if (index && *index != data::pak::core::kFallbackResourceIndex
        && *index != data::pak::core::kErrorTextureResourceIndex) {
        const auto* table = context.source_content
          ? context.source_content->GetTextureTable()
          : nullptr;
        if (!table || !table->IsValidKey(*index)) {
          throw std::runtime_error("Scene environment texture binding is "
                                   "outside its source texture table");
        }
      }
    }
  }
  return asset;
}

} // namespace oxygen::content::loaders
