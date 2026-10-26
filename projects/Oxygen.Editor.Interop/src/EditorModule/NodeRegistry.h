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
#include <vector>

#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <api_export.h>

namespace oxygen::scene {
  class Scene;
} // namespace oxygen::scene

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

  //! A node of the editor's scene: a registered node, or with a path a node
  //! an instance brings in below that node, one node ID per nesting level,
  //! shallowest first.
  struct NodeReferenceKey {
    UuidKey node {};
    std::vector<oxygen::data::NodeId> path;

    auto operator==(const NodeReferenceKey&) const -> bool = default;
  };

  struct NodeReferenceKeyHash {
    size_t operator()(const NodeReferenceKey& k) const noexcept {
      auto hash = UuidKeyHash {}(k.node);
      for (const auto& id : k.path) {
        hash = (hash * 31U) ^ std::hash<oxygen::data::NodeId> {}(id);
      }
      return hash;
    }
  };

  class NodeRegistry {
  public:
    OXGN_EI_API static void Register(const UuidKey& id,
      oxygen::scene::NodeHandle handle) noexcept;

    OXGN_EI_API static void Unregister(const UuidKey& id) noexcept;

    OXGN_EI_API static std::optional<oxygen::scene::NodeHandle>
      Lookup(const UuidKey& id) noexcept;

    //! The editor node id a native node was registered with.
    OXGN_EI_API static std::optional<UuidKey>
      ReverseLookup(const oxygen::scene::NodeHandle& handle) noexcept;

    //! The live node `reference` names: its registered node, then each level
    //! of its path found through that level's instance root.
    OXGN_EI_API static std::optional<oxygen::scene::NodeHandle>
      Lookup(const oxygen::scene::Scene& scene,
        const NodeReferenceKey& reference) noexcept;

    //! The reference of a live node: its nearest registered ancestor, and the
    //! node ID each instance root below that ancestor knows it by.
    OXGN_EI_API static std::optional<NodeReferenceKey>
      ReverseLookup(const oxygen::scene::Scene& scene,
        const oxygen::scene::NodeHandle& handle) noexcept;

    OXGN_EI_API static void ClearAll() noexcept;

  private:
    static std::mutex& Mutex();
    static std::unordered_map<UuidKey, oxygen::scene::NodeHandle, UuidKeyHash>&
      Map();
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
