//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content {

//! Reasons that trigger resource eviction notifications.
enum class EvictionReason : uint8_t {
  kRefCountZero,
  kClear,
  kShutdown,
};

//! Convert eviction reason to a stable string.
[[nodiscard]] inline auto to_string(EvictionReason reason) -> std::string_view
{
  switch (reason) {
  case EvictionReason::kRefCountZero:
    return "RefCountZero";
  case EvictionReason::kClear:
    return "Clear";
  case EvictionReason::kShutdown:
    return "Shutdown";
  }
  return "Unknown";
}

//! Payload emitted when a cached resource or asset is evicted.
struct EvictionEvent final {
  std::optional<data::AssetKey> asset_key {};
  ResourceKey key {};
  TypeId type_id { kInvalidTypeId };
  EvictionReason reason { EvictionReason::kRefCountZero };

#if !defined(NDEBUG)
  uint64_t cache_key_hash { 0 };
#endif
};

} // namespace oxygen::content
