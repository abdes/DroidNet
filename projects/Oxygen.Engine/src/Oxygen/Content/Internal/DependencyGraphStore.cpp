//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#ifndef NDEBUG
#  include <Oxygen/Base/Logging.h>
#endif
#include <Oxygen/Content/Internal/DependencyGraphStore.h>
#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content::internal {

auto DependencyGraphStore::Clear() -> void
{
  asset_dependencies_.clear();
  resource_dependencies_.clear();
}

auto DependencyGraphStore::AddAssetDependency(
  const uint64_t dependent, const uint64_t dependency) -> bool
{
  return asset_dependencies_[dependent].insert(dependency).second;
}

auto DependencyGraphStore::AddResourceDependency(
  const uint64_t dependent, const ResourceKey resource_key) -> bool
{
  return resource_dependencies_[dependent].insert(resource_key).second;
}

auto DependencyGraphStore::FindAssetDependencies(const uint64_t key) const
  -> const std::unordered_set<uint64_t>*
{
  if (const auto it = asset_dependencies_.find(key);
    it != asset_dependencies_.end()) {
    return &it->second;
  }
  return nullptr;
}

auto DependencyGraphStore::FindResourceDependencies(const uint64_t key) const
  -> const std::unordered_set<ResourceKey>*
{
  if (const auto it = resource_dependencies_.find(key);
    it != resource_dependencies_.end()) {
    return &it->second;
  }
  return nullptr;
}

auto DependencyGraphStore::RemoveAssetDependencies(const uint64_t key)
  -> std::optional<std::unordered_set<uint64_t>>
{
  const auto it = asset_dependencies_.find(key);
  if (it == asset_dependencies_.end()) {
    return std::nullopt;
  }
  auto out = std::move(it->second);
  asset_dependencies_.erase(it);
  return out;
}

auto DependencyGraphStore::RemoveResourceDependencies(const uint64_t key)
  -> std::optional<std::unordered_set<ResourceKey>>
{
  const auto it = resource_dependencies_.find(key);
  if (it == resource_dependencies_.end()) {
    return std::nullopt;
  }
  auto out = std::move(it->second);
  resource_dependencies_.erase(it);
  return out;
}

auto DependencyGraphStore::AssetDependencies() const -> const AssetDepsMap&
{
  return asset_dependencies_;
}

auto DependencyGraphStore::ResourceDependencies() const
  -> const ResourceDepsMap&
{
  return resource_dependencies_;
}

auto DependencyGraphStore::AssertEdgeRefcountSymmetry(std::string_view context,
  const std::function<uint32_t(uint64_t)>& get_checkout_count) const -> void
{
#ifndef NDEBUG
  for (const auto& [dependent, deps] : asset_dependencies_) {
    for (const auto& dep_key : deps) {
      if (get_checkout_count(dep_key) == 0U) {
        LOG_F(ERROR,
          "[invariant:{}] asset dependency edge has zero cache retains: "
          "dependent=0x{:016x} dependency=0x{:016x}",
          context, dependent, dep_key);
      }
    }
  }

  for (const auto& [dependent, deps] : resource_dependencies_) {
    for (const auto& res_key : deps) {
      const auto res_hash = res_key.get();
      if (get_checkout_count(res_hash) == 0U) {
        LOG_F(ERROR,
          "[invariant:{}] resource dependency edge has zero cache retains: "
          "dependent=0x{:016x} resource_hash=0x{:016x}",
          context, dependent, res_hash);
      }
    }
  }
#else
  static_cast<void>(context);
  static_cast<void>(get_checkout_count);
#endif
}

} // namespace oxygen::content::internal
