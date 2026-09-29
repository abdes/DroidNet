//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content::internal {

//! Edges use the same source-qualified identities as the asset cache.
class DependencyGraphStore final {
public:
  using AssetDepsMap
    = std::unordered_map<uint64_t, std::unordered_set<uint64_t>>;
  using ResourceDepsMap
    = std::unordered_map<uint64_t, std::unordered_set<ResourceKey>>;

  auto Clear() -> void;

  auto AddAssetDependency(uint64_t dependent, uint64_t dependency) -> bool;
  auto AddResourceDependency(uint64_t dependent, ResourceKey resource_key)
    -> bool;

  auto FindAssetDependencies(uint64_t key) const
    -> const std::unordered_set<uint64_t>*;
  auto FindResourceDependencies(uint64_t key) const
    -> const std::unordered_set<ResourceKey>*;

  auto RemoveAssetDependencies(uint64_t key)
    -> std::optional<std::unordered_set<uint64_t>>;
  auto RemoveResourceDependencies(uint64_t key)
    -> std::optional<std::unordered_set<ResourceKey>>;

  [[nodiscard]] auto AssetDependencies() const -> const AssetDepsMap&;
  [[nodiscard]] auto ResourceDependencies() const -> const ResourceDepsMap&;

  auto AssertEdgeRefcountSymmetry(std::string_view context,
    const std::function<uint32_t(uint64_t)>& get_checkout_count) const -> void;

private:
  AssetDepsMap asset_dependencies_;
  ResourceDepsMap resource_dependencies_;
};

} // namespace oxygen::content::internal
