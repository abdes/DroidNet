//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/NodeRegistry.h"

namespace oxygen::interop::module {

  static std::mutex g_mutex;
  static std::unordered_map<UuidKey, oxygen::scene::NodeHandle, UuidKeyHash>
    g_map;

  std::mutex& NodeRegistry::Mutex() { return g_mutex; }
  std::unordered_map<UuidKey, oxygen::scene::NodeHandle, UuidKeyHash>&
    NodeRegistry::Map() {
    return g_map;
  }

  void NodeRegistry::Register(const UuidKey& id,
    oxygen::scene::NodeHandle handle) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    Map().emplace(id, handle);
  }

  void NodeRegistry::Unregister(const UuidKey& id) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    Map().erase(id);
  }

  std::optional<oxygen::scene::NodeHandle>
    NodeRegistry::Lookup(const UuidKey& id) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    auto it = Map().find(id);
    if (it == Map().end())
      return std::nullopt;
    return it->second;
  }

  void NodeRegistry::ClearAll() noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    Map().clear();
  }

} // namespace oxygen::interop::module
