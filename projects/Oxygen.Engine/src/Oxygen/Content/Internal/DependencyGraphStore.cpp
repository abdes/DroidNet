//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/DependencyGraphStore.h>

namespace oxygen::content::internal {

auto DependencyGraphStore::Clear() -> void
{
  asset_dependencies_.clear();
  resource_dependencies_.clear();
}

auto DependencyGraphStore::AddAssetDependency(
  const data::AssetKey& dependent, const data::AssetKey& dependency) -> bool
{
  return asset_dependencies_[dependent].insert(dependency).second;
}

auto DependencyGraphStore::AddResourceDependency(
  const data::AssetKey& dependent, const ResourceKey resource_key) -> bool
{
  return resource_dependencies_[dependent].insert(resource_key).second;
}

auto DependencyGraphStore::FindAssetDependencies(
  const data::AssetKey& key) const -> const std::unordered_set<data::AssetKey>*
{
  if (const auto it = asset_dependencies_.find(key);
    it != asset_dependencies_.end()) {
    return &it->second;
  }
  return nullptr;
}

auto DependencyGraphStore::FindResourceDependencies(
  const data::AssetKey& key) const -> const std::unordered_set<ResourceKey>*
{
  if (const auto it = resource_dependencies_.find(key);
    it != resource_dependencies_.end()) {
    return &it->second;
  }
  return nullptr;
}

auto DependencyGraphStore::RemoveAssetDependencies(const data::AssetKey& key)
  -> std::optional<std::unordered_set<data::AssetKey>>
{
  const auto it = asset_dependencies_.find(key);
  if (it == asset_dependencies_.end()) {
    return std::nullopt;
  }
  auto out = std::move(it->second);
  asset_dependencies_.erase(it);
  return out;
}

auto DependencyGraphStore::RemoveResourceDependencies(const data::AssetKey& key)
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
  const std::function<std::optional<uint64_t>(const data::AssetKey&)>&
    resolve_asset_hash,
  const std::function<uint64_t(const data::AssetKey&)>& hash_asset_fallback,
  const std::function<uint64_t(const ResourceKey)>& hash_resource,
  const std::function<uint32_t(uint64_t)>& get_checkout_count) const -> void
{
#if !defined(NDEBUG)
  for (const auto& [dependent, deps] : asset_dependencies_) {
    for (const auto& dep_key : deps) {
      const auto dep_hash = resolve_asset_hash(dep_key);
      const auto resolved_hash
        = dep_hash.has_value() ? *dep_hash : hash_asset_fallback(dep_key);
      if (get_checkout_count(resolved_hash) == 0U) {
        LOG_F(ERROR,
          "[invariant:{}] asset dependency edge has zero cache retains: "
          "dependent={} dependency={} hash=0x{:016x}",
          context, data::to_string(dependent), data::to_string(dep_key),
          resolved_hash);
      }
    }
  }

  for (const auto& [dependent, deps] : resource_dependencies_) {
    for (const auto& res_key : deps) {
      const auto res_hash = hash_resource(res_key);
      if (get_checkout_count(res_hash) == 0U) {
        LOG_F(ERROR,
          "[invariant:{}] resource dependency edge has zero cache retains: "
          "dependent={} resource_hash=0x{:016x}",
          context, data::to_string(dependent), res_hash);
      }
    }
  }
#else
  static_cast<void>(context);
  static_cast<void>(resolve_asset_hash);
  static_cast<void>(hash_asset_fallback);
  static_cast<void>(hash_resource);
  static_cast<void>(get_checkout_count);
#endif
}

} // namespace oxygen::content::internal
