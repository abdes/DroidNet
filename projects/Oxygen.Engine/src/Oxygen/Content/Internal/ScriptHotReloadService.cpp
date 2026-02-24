//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <ranges>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/ScriptHotReloadService.h>

namespace oxygen::content::internal {

ScriptHotReloadService::ScriptHotReloadService(
  std::optional<PathFinder> path_finder)
  : path_finder_(std::move(path_finder))
{
}

auto ScriptHotReloadService::Subscribe(
  const uint64_t id, ScriptReloadCallback callback) -> void
{
  subscribers_.push_back(Subscriber {
    .id = id,
    .handler = std::move(callback),
  });
}

auto ScriptHotReloadService::Unsubscribe(const uint64_t id) -> void
{
  const auto erase_from
    = std::remove_if(subscribers_.begin(), subscribers_.end(),
      [id](const Subscriber& subscriber) { return subscriber.id == id; });
  subscribers_.erase(erase_from, subscribers_.end());
}

auto ScriptHotReloadService::NormalizePathString(std::string_view path)
  -> std::string
{
  std::string normalized(path);
  while (!normalized.empty() && normalized[0] == '@') {
    normalized = normalized.substr(1);
  }
  std::ranges::replace(normalized, '\\', '/');
  while (!normalized.empty() && normalized[0] == '/') {
    normalized = normalized.substr(1);
  }
  return std::filesystem::path(normalized).lexically_normal().generic_string();
}

auto ScriptHotReloadService::TryMapChangedPathToRelative(
  const std::filesystem::path& changed_path) const -> std::optional<std::string>
{
  if (!path_finder_.has_value()) {
    return std::nullopt;
  }

  const std::filesystem::path absolute_changed_path
    = std::filesystem::absolute(changed_path).lexically_normal();

  for (const auto& root : path_finder_->ScriptSourceRoots()) {
    const auto abs_root = std::filesystem::absolute(root).lexically_normal();
    auto [root_end, changed_end]
      = std::ranges::mismatch(abs_root, absolute_changed_path);
    if (root_end != abs_root.end()) {
      continue;
    }

    std::filesystem::path rel;
    while (changed_end != absolute_changed_path.end()) {
      rel /= *changed_end++;
    }
    return rel.generic_string();
  }

  return std::nullopt;
}

auto ScriptHotReloadService::RebuildPathIndex(const ReloadCallbacks& callbacks)
  -> void
{
  for (const auto& key : callbacks.enumerate_loaded_script_keys()) {
    if (auto asset = callbacks.get_script_asset(key)) {
      if (const auto path_opt = asset->TryGetExternalSourcePath();
        path_opt.has_value()) {
        script_path_to_asset_key_.insert_or_assign(
          NormalizePathString(path_opt.value()), key);
      }
    }
  }
}

auto ScriptHotReloadService::NotifyReloadedScript(const data::AssetKey& key,
  const std::shared_ptr<data::ScriptAsset>& asset,
  const ReloadCallbacks& callbacks) -> void
{
  if (!asset) {
    LOG_F(ERROR, "failed to reload script asset");
    return;
  }

  if (const auto path_opt = asset->TryGetExternalSourcePath();
    path_opt.has_value()) {
    script_path_to_asset_key_.insert_or_assign(
      NormalizePathString(path_opt.value()), key);
  }

  const auto bytecode_index = asset->GetBytecodeResourceIndex();
  if (bytecode_index == data::pak::kNoResourceIndex) {
    return;
  }

  const auto source_id = callbacks.resolve_source_id_for_asset(key);
  if (!source_id.has_value()) {
    return;
  }

  const auto resource_key
    = callbacks.make_script_resource_key(*source_id, bytecode_index);
  auto resource = callbacks.get_script_resource(resource_key);
  if (!resource) {
    return;
  }

  for (const auto& sub : subscribers_) {
    if (sub.handler) {
      sub.handler(key, resource);
    }
  }
}

auto ScriptHotReloadService::ReloadScript(
  const std::filesystem::path& changed_path, const ReloadCallbacks& callbacks)
  -> void
{
  const auto absolute_changed_path
    = std::filesystem::absolute(changed_path).lexically_normal();
  LOG_F(INFO, "change detected at {}", absolute_changed_path.generic_string());

  const auto relative_changed_path = TryMapChangedPathToRelative(changed_path);
  if (!relative_changed_path.has_value()) {
    LOG_F(WARNING,
      "changed file is not within any registered ScriptSourceRoot: {}",
      absolute_changed_path.string());
    return;
  }
  LOG_F(INFO, "mapped to relative path '{}'", *relative_changed_path);

  const auto normalized_target = NormalizePathString(*relative_changed_path);
  auto key_it = script_path_to_asset_key_.find(normalized_target);
  if (key_it == script_path_to_asset_key_.end()) {
    RebuildPathIndex(callbacks);
    key_it = script_path_to_asset_key_.find(normalized_target);
  }
  if (key_it == script_path_to_asset_key_.end()) {
    LOG_F(WARNING, "no matching asset found for relative path '{}'",
      normalized_target);
    return;
  }

  const auto target_key = key_it->second;
  LOG_F(INFO, "reloading script asset key={}", data::to_string(target_key));

  callbacks.invalidate_asset_tree(target_key);
  callbacks.start_load_script_asset(target_key,
    [this, target_key, callbacks](std::shared_ptr<data::ScriptAsset> asset) {
      NotifyReloadedScript(target_key, asset, callbacks);
    });
}

auto ScriptHotReloadService::ReloadAllScripts(const ReloadCallbacks& callbacks)
  -> void
{
  LOG_F(INFO, "triggering reload of all script assets");
  const auto script_keys = callbacks.enumerate_loaded_script_keys();
  for (const auto& key : script_keys) {
    callbacks.invalidate_asset_tree(key);
    callbacks.start_load_script_asset(
      key, [this, key, callbacks](std::shared_ptr<data::ScriptAsset> asset) {
        NotifyReloadedScript(key, asset, callbacks);
      });
  }
}

} // namespace oxygen::content::internal
