//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

class DependencyGraphStore final {
public:
  using AssetDepsMap
    = std::unordered_map<data::AssetKey, std::unordered_set<data::AssetKey>>;
  using ResourceDepsMap
    = std::unordered_map<data::AssetKey, std::unordered_set<ResourceKey>>;

  auto Clear() -> void;

  auto AddAssetDependency(
    const data::AssetKey& dependent, const data::AssetKey& dependency) -> bool;
  auto AddResourceDependency(
    const data::AssetKey& dependent, ResourceKey resource_key) -> bool;

  auto FindAssetDependencies(const data::AssetKey& key) const
    -> const std::unordered_set<data::AssetKey>*;
  auto FindResourceDependencies(const data::AssetKey& key) const
    -> const std::unordered_set<ResourceKey>*;

  auto RemoveAssetDependencies(const data::AssetKey& key)
    -> std::optional<std::unordered_set<data::AssetKey>>;
  auto RemoveResourceDependencies(const data::AssetKey& key)
    -> std::optional<std::unordered_set<ResourceKey>>;

  [[nodiscard]] auto AssetDependencies() const -> const AssetDepsMap&;
  [[nodiscard]] auto ResourceDependencies() const -> const ResourceDepsMap&;

  auto AssertEdgeRefcountSymmetry(std::string_view context,
    const std::function<std::optional<uint64_t>(const data::AssetKey&)>&
      resolve_asset_hash,
    const std::function<uint64_t(const data::AssetKey&)>& hash_asset_fallback,
    const std::function<uint64_t(ResourceKey)>& hash_resource,
    const std::function<uint32_t(uint64_t)>& get_checkout_count) const -> void;

private:
  AssetDepsMap asset_dependencies_;
  ResourceDepsMap resource_dependencies_;
};

} // namespace oxygen::content::internal
