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

auto EvictionRegistry::IsSubscribed(
  const TypeId type_id, const uint64_t id) const noexcept -> bool
{
  const auto found = subscribers_.find(type_id);
  return found != subscribers_.end()
    && std::ranges::any_of(found->second,
      [id](const Subscriber& subscriber) { return subscriber.id == id; });
}

auto EvictionRegistry::TryEnterEviction(ActiveEviction& scope) noexcept -> bool
{
  for (const auto* active = active_eviction_; active != nullptr;
    active = active->previous) {
    if (active->key == scope.key) {
      return false;
    }
  }
  scope.previous = active_eviction_;
  active_eviction_ = &scope;
  return true;
}

auto EvictionRegistry::ExitEviction(const ActiveEviction& scope) noexcept
  -> void
{
  if (active_eviction_ == &scope) {
    active_eviction_ = scope.previous;
  }
}

auto EvictionRegistry::Clear() -> void
{
  [[maybe_unused]] auto retired = std::exchange(subscribers_, {});
  active_eviction_ = nullptr;
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
