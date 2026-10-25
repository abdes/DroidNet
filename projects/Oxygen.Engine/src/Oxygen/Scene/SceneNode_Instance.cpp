//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneInstanceComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

namespace oxygen::scene {

namespace {
  auto FindComponent(SceneNode& node) -> observer_ptr<SceneInstanceComponent>
  {
    if (!node.IsAlive()) {
      return nullptr;
    }
    const auto impl = node.GetImpl();
    if (!impl.has_value()
      || !impl->get().HasComponent<SceneInstanceComponent>()) {
      return nullptr;
    }
    return observer_ptr {
      &impl->get().GetComponent<SceneInstanceComponent>(),
    };
  }
} // namespace

auto SceneNode::GetInstance() & noexcept -> Instance { return Instance(*this); }

auto SceneNode::GetInstance() const& noexcept -> Instance
{
  // NOLINTNEXTLINE(*-pro-type-const-cast)
  return Instance(const_cast<SceneNode&>(*this));
}

auto SceneNode::Instance::IsInstanceRoot() const noexcept -> bool
{
  return FindComponent(*node_) != nullptr;
}

auto SceneNode::Instance::Source() const noexcept
  -> std::shared_ptr<const data::SceneAsset>
{
  const auto component = FindComponent(*node_);
  return component != nullptr ? component->Source() : nullptr;
}

auto SceneNode::Instance::Changes() const noexcept
  -> std::span<const data::SceneChange>
{
  const auto component = FindComponent(*node_);
  return component != nullptr ? component->Changes()
                              : std::span<const data::SceneChange> {};
}

auto SceneNode::Instance::Find(const std::span<const data::NodeId> path) const
  -> std::optional<SceneNode>
{
  const auto component = FindComponent(*node_);
  const auto scene = node_->scene_weak_.lock();
  if (component == nullptr || path.empty() || !scene) {
    return std::nullopt;
  }
  const auto handle = component->FindNode(path.front());
  if (!handle.has_value()) {
    return std::nullopt;
  }
  auto found = scene->GetNode(*handle);
  if (!found.has_value() || path.size() == 1U) {
    return found;
  }
  return found->GetInstance().Find(path.subspan(1U));
}

} // namespace oxygen::scene
