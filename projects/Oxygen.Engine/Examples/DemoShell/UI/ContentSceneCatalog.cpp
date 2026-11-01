//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DemoShell/UI/ContentSceneCatalog.h"
#include "DemoShell/UI/ContentTextMatch.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples::ui {

namespace {
  auto ToEntry(const content::IAssetLoader::MountedSceneEntry& scene)
    -> SceneEntry
  {
    const auto& path
      = scene.virtual_path.empty() ? scene.display_name : scene.virtual_path;
    // `/<mount>/<folder>/<middle>.../<leaf>`.
    auto parts = std::vector<std::string> {};
    for (const auto& part : std::filesystem::path(path).relative_path()) {
      parts.push_back(part.generic_string());
    }
    auto name = std::filesystem::path(path).stem().string();
    auto folder = parts.size() > 2 ? parts.at(1) : std::string {};
    auto label = std::string {};
    for (std::size_t index = 2; index + 1 < parts.size(); ++index) {
      if (!label.empty()) {
        label += " / ";
      }
      label += parts.at(index);
    }
    if (label.empty()) {
      label = name;
    }
    return SceneEntry {
      .name = std::move(name),
      .virtual_path = scene.virtual_path,
      .folder = std::move(folder),
      .label = std::move(label),
      .key = scene.scene_key,
      .source_kind
      = scene.source_kind == content::IAssetLoader::ContentSourceKind::kPak
        ? data::CookedSourceKind::kPak
        : data::CookedSourceKind::kLooseCooked,
      .source_path = scene.source_path,
    };
  }
} // namespace

auto CountSceneAssets(const data::AssetKey& key,
  const std::vector<std::filesystem::path>& sources) -> std::size_t
{
  auto opened = std::vector<content::cooked::CookedContent> {};
  for (const auto& source : sources) {
    try {
      opened.push_back(content::cooked::CookedContent::Open(
        content::cooked::RealCookerStorage(), source));
    } catch (const std::exception& error) {
      LOG_F(
        INFO, "Scene assets: skipping '{}': {}", source.string(), error.what());
    }
  }
  const auto find
    = [&opened](
        const data::AssetKey& asset) -> const content::cooked::CookedAsset* {
    for (const auto& cooked : opened) {
      if (const auto* found = cooked.FindAsset(asset)) {
        return found;
      }
    }
    return nullptr;
  };
  auto seen = std::unordered_set<data::AssetKey> {};
  auto pending = std::vector { key };
  while (!pending.empty()) {
    const auto asset = pending.back();
    pending.pop_back();
    const auto* found = find(asset);
    if (found == nullptr || !seen.insert(asset).second) {
      continue;
    }
    for (const auto& reference : found->references.Keys()) {
      if (reference.kind == data::KeyReferenceKind::kAsset) {
        pending.push_back(reference.key);
      }
    }
  }
  return seen.size();
}

auto ContentSceneCatalog::Rebuild(
  const std::vector<content::IAssetLoader::MountedSceneEntry>& scenes,
  const Precedence& precedence) -> void
{
  constexpr auto kOutside = std::numeric_limits<std::size_t>::max();
  struct Candidate {
    SceneEntry entry;
    std::size_t rank { kOutside };
  };
  auto top = std::unordered_map<data::AssetKey, Candidate> {};
  for (const auto& scene : scenes) {
    const auto rank = precedence
      ? precedence(scene.source_path).value_or(kOutside)
      : kOutside;
    const auto found = top.find(scene.scene_key);
    if (found == top.end() || rank < found->second.rank) {
      top.insert_or_assign(
        scene.scene_key, Candidate { .entry = ToEntry(scene), .rank = rank });
    }
  }

  auto by_mount = std::map<std::string, SceneGroup> {};
  for (auto& candidate : top | std::views::values) {
    // The mount keeps its leading slash, such as `/Showcase`.
    const auto root = content::MountRootOf(candidate.entry.virtual_path);
    const auto mount = std::string(root.empty() ? "/?" : root);
    auto& group = by_mount[mount];
    group.mount = mount;
    if (candidate.entry.source_kind == data::CookedSourceKind::kPak) {
      group.from_pak = true;
    } else {
      group.from_loose_root = true;
    }
    group.scenes.push_back(std::move(candidate.entry));
  }
  groups_.clear();
  for (auto& group : by_mount | std::views::values) {
    std::ranges::sort(group.scenes, {}, &SceneEntry::virtual_path);
    groups_.push_back(std::move(group));
  }
}

auto ContentSceneCatalog::Count() const -> std::size_t
{
  auto count = std::size_t { 0 };
  for (const auto& group : groups_) {
    count += group.scenes.size();
  }
  return count;
}

auto ContentSceneCatalog::Find(const data::AssetKey& key) const
  -> const SceneEntry*
{
  for (const auto& group : groups_) {
    const auto found = std::ranges::find(group.scenes, key, &SceneEntry::key);
    if (found != group.scenes.end()) {
      return &*found;
    }
  }
  return nullptr;
}

auto ContentSceneCatalog::Matches(
  const SceneEntry& scene, const TextFilter& filter) -> bool
{
  return filter.Matches(scene.name) || filter.Matches(scene.virtual_path);
}

} // namespace oxygen::examples::ui
