//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content {

//! Reasons that trigger resource eviction notifications.
enum class EvictionReason : uint8_t {
  kRefCountZero,
  kClear,
  kShutdown,
};

//! Payload emitted when a cached resource is evicted.
struct EvictionEvent final {
  ResourceKey key {};
  TypeId type_id { kInvalidTypeId };
  EvictionReason reason { EvictionReason::kRefCountZero };

#if !defined(NDEBUG)
  uint64_t cache_key_hash { 0 };
#endif
};

} // namespace oxygen::content
