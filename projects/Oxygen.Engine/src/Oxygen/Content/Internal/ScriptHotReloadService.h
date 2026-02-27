//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>

namespace oxygen::content::internal {

class ScriptHotReloadService final {
public:
  using ScriptReloadCallback = IAssetLoader::ScriptReloadCallback;

  struct ReloadCallbacks final {
    std::function<std::vector<data::AssetKey>()> enumerate_loaded_script_keys;
    std::function<std::shared_ptr<data::ScriptAsset>(const data::AssetKey&)>
      get_script_asset;
    std::function<void(const data::AssetKey&)> invalidate_asset_tree;
    std::function<void(const data::AssetKey&,
      std::function<void(std::shared_ptr<data::ScriptAsset>)>)>
      start_load_script_asset;
    std::function<std::optional<uint16_t>(const data::AssetKey&)>
      resolve_source_id_for_asset;
    std::function<ResourceKey(uint16_t, data::pak::core::ResourceIndexT)>
      make_script_resource_key;
    std::function<std::shared_ptr<data::ScriptResource>(ResourceKey)>
      get_script_resource;
  };

  explicit ScriptHotReloadService(std::optional<PathFinder> path_finder);

  auto Subscribe(uint64_t id, ScriptReloadCallback callback) -> void;
  auto Unsubscribe(uint64_t id) -> void;

  auto ReloadScript(const std::filesystem::path& changed_path,
    const ReloadCallbacks& callbacks) -> void;
  auto ReloadAllScripts(const ReloadCallbacks& callbacks) -> void;

private:
  struct Subscriber final {
    uint64_t id { 0 };
    ScriptReloadCallback handler;
  };

  auto NormalizePathString(std::string_view path) -> std::string;
  auto TryMapChangedPathToRelative(
    const std::filesystem::path& changed_path) const
    -> std::optional<std::string>;
  auto RebuildPathIndex(const ReloadCallbacks& callbacks) -> void;
  auto NotifyReloadedScript(const data::AssetKey& key,
    const std::shared_ptr<data::ScriptAsset>& asset,
    const ReloadCallbacks& callbacks) -> void;

  std::optional<PathFinder> path_finder_;
  std::unordered_map<std::string, data::AssetKey> script_path_to_asset_key_ {};
  std::vector<Subscriber> subscribers_ {};
};

} // namespace oxygen::content::internal
