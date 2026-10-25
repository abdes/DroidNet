//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::scene {

//! Makes its node an instance root: the root nodes of a source scene are its
//! children.
/*!
 Holds the source scene, kept loaded so a change can be cleared by reading the
 source again; the changes made below the root; and the nodes the source
 brought in, by node ID, one level deep. A nested instance root has its own
 component.

 A change is the instancing scene's, the requester, whose tables its values
 use and whose live nodes, by node index, its node values name; or a host's,
 whose values name no table: its node values name the host nodes, and its
 LOD policies the host policies, by position. Every change of an instance a
 host made is a host's.
*/
class SceneInstanceComponent final : public Component {
  OXYGEN_COMPONENT(SceneInstanceComponent)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  //! `requester_nodes` are the requester's live nodes by node index, shared
  //! by the instance roots of one requester; null for a host's instance.
  SceneInstanceComponent(std::shared_ptr<const data::SceneAsset> source,
    std::shared_ptr<const data::SceneAsset> requester,
    std::shared_ptr<const std::vector<SceneNode>> requester_nodes,
    std::vector<data::SceneChange> changes,
    std::unordered_map<data::NodeId, NodeHandle> nodes,
    std::vector<SceneNode> host_nodes = {},
    std::vector<data::pak::geometry::LodPolicyRecord> host_lod_policies = {})
    : source_(std::move(source))
    , requester_(std::move(requester))
    , requester_nodes_(std::move(requester_nodes))
    , changes_(std::move(changes))
    , nodes_(std::move(nodes))
    , host_nodes_(std::move(host_nodes))
    , host_lod_policies_(std::move(host_lod_policies))
  {
  }

  ~SceneInstanceComponent() override = default;

  OXYGEN_MAKE_NON_COPYABLE(SceneInstanceComponent)
  OXYGEN_DEFAULT_MOVABLE(SceneInstanceComponent)

  [[nodiscard]] auto Source() const noexcept
    -> const std::shared_ptr<const data::SceneAsset>&
  {
    return source_;
  }

  //! The scene that makes the changes; null when a host made the instance.
  [[nodiscard]] auto Requester() const noexcept
    -> const std::shared_ptr<const data::SceneAsset>&
  {
    return requester_;
  }

  //! The requester's live nodes, by node index; empty for a host's instance.
  [[nodiscard]] auto RequesterNodes() const noexcept
    -> std::span<const SceneNode>
  {
    return requester_nodes_ ? std::span<const SceneNode>(*requester_nodes_)
                            : std::span<const SceneNode> {};
  }

  //! The nodes a host's node values name, by position.
  [[nodiscard]] auto HostNodes() const noexcept -> std::span<const SceneNode>
  {
    return host_nodes_;
  }

  //! Replaces the host nodes; positions a held change names must stay.
  auto SetHostNodes(std::vector<SceneNode> nodes) -> void
  {
    host_nodes_ = std::move(nodes);
  }

  //! The LOD policies a host's LOD policy changes name, by position.
  [[nodiscard]] auto HostLodPolicies() const noexcept
    -> std::span<const data::pak::geometry::LodPolicyRecord>
  {
    return host_lod_policies_;
  }

  //! Replaces the host LOD policies; positions a held change names must stay.
  auto SetHostLodPolicies(
    std::vector<data::pak::geometry::LodPolicyRecord> policies) -> void
  {
    host_lod_policies_ = std::move(policies);
  }

  //! The changes below the root.
  [[nodiscard]] auto Changes() const noexcept
    -> std::span<const data::SceneChange>
  {
    return changes_;
  }

  //! True when a host made the change with `address`.
  [[nodiscard]] auto IsHostChange(const data::ChangeAddress& address) const
    -> bool
  {
    // The C++20 editor interop reads this header: no `std::ranges::contains`.
    // NOLINTBEGIN(*contains*)
    return !requester_
      || std::ranges::find(by_host_, address) != by_host_.end();
    // NOLINTEND(*contains*)
  }

  //! Adds `change`, or replaces the change with its address; `by_host` says a
  //! host made it.
  auto SetChange(data::SceneChange change, const bool by_host) -> void
  {
    const auto address = data::AddressOf(change);
    std::erase(by_host_, address);
    if (by_host) {
      by_host_.push_back(address);
    }
    const auto found = std::ranges::find_if(
      changes_, [&address](const data::SceneChange& entry) -> bool {
        return data::AddressOf(entry) == address;
      });
    if (found != changes_.end()) {
      *found = std::move(change);
      return;
    }
    changes_.push_back(std::move(change));
  }

  //! Removes the change with `address`; false when there is none.
  auto ClearChange(const data::ChangeAddress& address) -> bool
  {
    std::erase(by_host_, address);
    return std::erase_if(changes_,
             [&address](const data::SceneChange& entry) -> bool {
               return data::AddressOf(entry) == address;
             })
      != 0U;
  }

  //! The node the source's node `id` became, if it was created.
  [[nodiscard]] auto FindNode(const data::NodeId& id) const
    -> std::optional<NodeHandle>
  {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  //! The node ID of the source's node that became `handle`, if it is one.
  [[nodiscard]] auto IdOf(const NodeHandle& handle) const
    -> std::optional<data::NodeId>
  {
    const auto found = std::ranges::find(
      nodes_, handle, [](const auto& entry) -> auto { return entry.second; });
    if (found == nodes_.end()) {
      return std::nullopt;
    }
    return found->first;
  }

private:
  std::shared_ptr<const data::SceneAsset> source_;
  std::shared_ptr<const data::SceneAsset> requester_;
  std::shared_ptr<const std::vector<SceneNode>> requester_nodes_;
  std::vector<data::SceneChange> changes_;
  //! The addresses of the changes a host made, for a requester's instance.
  std::vector<data::ChangeAddress> by_host_;
  std::unordered_map<data::NodeId, NodeHandle> nodes_;
  std::vector<SceneNode> host_nodes_;
  std::vector<data::pak::geometry::LodPolicyRecord> host_lod_policies_;
};

} // namespace oxygen::scene
