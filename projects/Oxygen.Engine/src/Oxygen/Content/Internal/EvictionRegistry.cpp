//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/EvictionRegistry.h>
#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content::internal {

auto EvictionRegistry::AddSubscriber(const TypeId type_id, const uint64_t id,
  IAssetLoader::EvictionHandler handler) -> void
{
  auto& list = subscribers_[type_id];
  list.push_back(Subscriber {
    .id = id,
    .handler = std::move(handler),
  });
}

auto EvictionRegistry::RemoveSubscriber(const TypeId type_id, const uint64_t id)
  -> void
{
  auto it = subscribers_.find(type_id);
  if (it == subscribers_.end()) {
    return;
  }

  auto& list = it->second;
  const auto erase_from = std::remove_if(
    list.begin(), list.end(), [id](const Subscriber& s) { return s.id == id; });
  list.erase(erase_from, list.end());
  if (list.empty()) {
    subscribers_.erase(it);
  }
}

auto EvictionRegistry::SnapshotSubscribers(const TypeId type_id) const
  -> std::vector<Subscriber>
{
  const auto it = subscribers_.find(type_id);
  if (it == subscribers_.end()) {
    return {};
  }
  return it->second;
}

auto EvictionRegistry::TryEnterEviction(const uint64_t cache_key) -> bool
{
  return eviction_in_progress_.insert(cache_key).second;
}

auto EvictionRegistry::ExitEviction(const uint64_t cache_key) -> void
{
  eviction_in_progress_.erase(cache_key);
}

auto EvictionRegistry::Clear() -> void
{
  subscribers_.clear();
  eviction_in_progress_.clear();
  tracked_resources_.clear();
}

auto EvictionRegistry::TrackResource(const ResourceKey key) -> void
{
  tracked_resources_.insert(key);
}

auto EvictionRegistry::ForgetResource(const ResourceKey key) -> void
{
  tracked_resources_.erase(key);
}

auto EvictionRegistry::TrackedResources() const
  -> const std::unordered_set<ResourceKey>&
{
  return tracked_resources_;
}

} // namespace oxygen::content::internal
