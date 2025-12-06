//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module {

  using UuidKey = std::array<uint8_t, 16>;

  struct UuidKeyHash {
    size_t operator()(const UuidKey& k) const noexcept {
      uint64_t a = 0, b = 0;
      static_assert(sizeof(a) + sizeof(b) == 16);
      std::memcpy(&a, k.data(), 8);
      std::memcpy(&b, k.data() + 8, 8);
      return std::hash<uint64_t>{}(a) ^ (std::hash<uint64_t>{}(b) << 1);
    }
  };

  class NodeRegistry {
  public:
    static void Register(const UuidKey& id,
      oxygen::scene::NodeHandle handle) noexcept;

    static void Unregister(const UuidKey& id) noexcept;

    static std::optional<oxygen::scene::NodeHandle>
      Lookup(const UuidKey& id) noexcept;

    static void ClearAll() noexcept;

  private:
    static std::mutex& Mutex();
    static std::unordered_map<UuidKey, oxygen::scene::NodeHandle, UuidKeyHash>&
      Map();
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
