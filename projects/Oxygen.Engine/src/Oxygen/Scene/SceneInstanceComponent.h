//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::scene {

//! Makes its node an instance root: the root nodes of a source scene are its
//! children.
/*!
 Holds the source scene, kept loaded so a change can be cleared by reading the
 source again; the changes the instancing scene makes below the root; and the
 nodes the source brought in, by node ID, one level deep. A nested instance
 root has its own component.
*/
class SceneInstanceComponent final : public Component {
  OXYGEN_COMPONENT(SceneInstanceComponent)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  SceneInstanceComponent(std::shared_ptr<const data::SceneAsset> source,
    std::vector<data::SceneChange> changes,
    std::unordered_map<data::NodeId, NodeHandle> nodes)
    : source_(std::move(source))
    , changes_(std::move(changes))
    , nodes_(std::move(nodes))
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

  //! The instancing scene's changes below the root.
  [[nodiscard]] auto Changes() const noexcept
    -> std::span<const data::SceneChange>
  {
    return changes_;
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

private:
  std::shared_ptr<const data::SceneAsset> source_;
  std::vector<data::SceneChange> changes_;
  std::unordered_map<data::NodeId, NodeHandle> nodes_;
};

} // namespace oxygen::scene
