//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content::internal {

class ResourceKeyRegistry final {
public:
  auto InsertOrAssign(uint64_t hash, ResourceKey key) -> void;
  auto Erase(uint64_t hash) -> void;
  auto Clear() -> void;

  [[nodiscard]] auto Find(uint64_t hash) const -> std::optional<ResourceKey>;
  [[nodiscard]] auto Entries() const
    -> const std::unordered_map<uint64_t, ResourceKey>&;
  [[nodiscard]] auto Empty() const -> bool;
  [[nodiscard]] auto Size() const -> size_t;

  auto AssertConsistency(std::string_view context,
    const std::function<uint64_t(ResourceKey)>& hash_resource,
    const std::function<bool(uint64_t)>& cache_contains) const -> void;

private:
  std::unordered_map<uint64_t, ResourceKey> mapping_ {};
};

} // namespace oxygen::content::internal
