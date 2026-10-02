//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/IAssetLoader.h>

namespace oxygen::content::internal {

class EvictionRegistry final {
public:
  struct Subscriber final {
    uint64_t id { 0 };
    IAssetLoader::EvictionHandler handler;
  };

  struct ActiveEviction final {
    uint64_t key = 0;
    ActiveEviction* previous = nullptr;
  };

  auto AddSubscriber(
    TypeId type_id, uint64_t id, IAssetLoader::EvictionHandler handler) -> void;
  auto RemoveSubscriber(TypeId type_id, uint64_t id) -> void;
  [[nodiscard]] auto SnapshotSubscribers(TypeId type_id) const
    -> std::vector<Subscriber>;
  [[nodiscard]] auto IsSubscribed(TypeId type_id, uint64_t id) const noexcept
    -> bool;

  auto TryEnterEviction(ActiveEviction& scope) noexcept -> bool;
  auto ExitEviction(const ActiveEviction& scope) noexcept -> void;

  auto TrackResource(ResourceKey key) -> void;
  auto ForgetResource(ResourceKey key) -> void;
  [[nodiscard]] auto TrackedResources() const
    -> const std::unordered_set<ResourceKey>&;

  auto Clear() -> void;

private:
  std::unordered_map<TypeId, std::vector<Subscriber>> subscribers_ {};
  ActiveEviction* active_eviction_ = nullptr;
  std::unordered_set<ResourceKey> tracked_resources_ {};
};

} // namespace oxygen::content::internal
