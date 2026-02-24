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

  auto AddSubscriber(
    TypeId type_id, uint64_t id, IAssetLoader::EvictionHandler handler) -> void;
  auto RemoveSubscriber(TypeId type_id, uint64_t id) -> void;
  [[nodiscard]] auto SnapshotSubscribers(TypeId type_id) const
    -> std::vector<Subscriber>;

  auto TryEnterEviction(uint64_t cache_key) -> bool;
  auto ExitEviction(uint64_t cache_key) -> void;

  auto Clear() -> void;

private:
  std::unordered_map<TypeId, std::vector<Subscriber>> subscribers_ {};
  std::unordered_set<uint64_t> eviction_in_progress_ {};
};

} // namespace oxygen::content::internal
