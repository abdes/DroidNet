//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <algorithm>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneInstanceComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

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

  namespace {
    //! The node ID the instance root `ancestor` knows `node` by, if it is an
    //! instance root that brought `node` in.
    auto IdBelow(oxygen::scene::SceneNode& ancestor,
      const oxygen::scene::NodeHandle& node)
      -> std::optional<oxygen::data::NodeId>
    {
      const auto impl = ancestor.GetImpl();
      if (!impl.has_value()
        || !impl->get().HasComponent<oxygen::scene::SceneInstanceComponent>()) {
        return std::nullopt;
      }
      return impl->get()
        .GetComponent<oxygen::scene::SceneInstanceComponent>()
        .IdOf(node);
    }
  } // namespace

  std::optional<oxygen::scene::NodeHandle> NodeRegistry::Lookup(
    const oxygen::scene::Scene& scene,
    const NodeReferenceKey& reference) noexcept {
    const auto handle = Lookup(reference.node);
    if (!handle.has_value() || reference.path.empty()) {
      return handle;
    }
    auto root = scene.GetNode(*handle);
    if (!root.has_value()) {
      return std::nullopt;
    }
    const auto found = root->GetInstance().Find(reference.path);
    if (!found.has_value()) {
      return std::nullopt;
    }
    return found->GetHandle();
  }

  std::optional<NodeReferenceKey> NodeRegistry::ReverseLookup(
    const oxygen::scene::Scene& scene,
    const oxygen::scene::NodeHandle& handle) noexcept {
    auto node = scene.GetNode(handle);
    if (!node.has_value()) {
      return std::nullopt;
    }
    // Innermost level first; each level is found through the nearest
    // ancestor that is an instance root knowing the node.
    auto path = std::vector<oxygen::data::NodeId> {};
    while (true) {
      if (const auto id = ReverseLookup(node->GetHandle())) {
        std::ranges::reverse(path);
        return NodeReferenceKey { .node = *id, .path = std::move(path) };
      }
      auto ancestor = node->GetParent();
      auto id = std::optional<oxygen::data::NodeId> {};
      while (ancestor.has_value()) {
        id = IdBelow(*ancestor, node->GetHandle());
        if (id.has_value()) {
          break;
        }
        ancestor = ancestor->GetParent();
      }
      if (!id.has_value()) {
        return std::nullopt;
      }
      path.push_back(*id);
      node = std::move(ancestor);
    }
  }

  void NodeRegistry::ClearAll() noexcept {
    std::lock_guard<std::mutex> lk(Mutex());
    Map().clear();
    g_ids.clear();
  }

} // namespace oxygen::interop::module
