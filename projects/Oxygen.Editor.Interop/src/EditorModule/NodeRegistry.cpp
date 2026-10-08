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
  //! Inverse of g_map: the editor node id a native node was registered with.
  static std::unordered_map<oxygen::scene::NodeHandle, UuidKey> g_ids;

  std::mutex& NodeRegistry::Mutex() { return g_mutex; }
  std::unordered_map<UuidKey, oxygen::scene::NodeHandle, UuidKeyHash>&
    NodeRegistry::Map() {
    return g_map;
  }

  void NodeRegistry::Register(const UuidKey& id,
    oxygen::scene::NodeHandle handle) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    if (Map().emplace(id, handle).second) {
      g_ids.insert_or_assign(handle, id);
    }
  }

  void NodeRegistry::Unregister(const UuidKey& id) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    if (const auto it = Map().find(id); it != Map().end()) {
      g_ids.erase(it->second);
      Map().erase(it);
    }
  }

  std::optional<oxygen::scene::NodeHandle>
    NodeRegistry::Lookup(const UuidKey& id) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    auto it = Map().find(id);
    if (it == Map().end())
      return std::nullopt;
    return it->second;
  }

  std::optional<UuidKey> NodeRegistry::ReverseLookup(
    const oxygen::scene::NodeHandle& handle) noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    const auto it = g_ids.find(handle);
    if (it == g_ids.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void NodeRegistry::ClearAll() noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    Map().clear();
    g_ids.clear();
  }

} // namespace oxygen::interop::module
