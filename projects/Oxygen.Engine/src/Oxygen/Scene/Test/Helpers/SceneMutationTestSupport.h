//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Internal/ScriptSlotMutationProcessor.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::scene::testing_support {

class MutationObserver final : public ISceneObserver {
public:
  auto OnTransformChanged(const NodeHandle& node_handle) noexcept
    -> void override
  {
    transform_changed.push_back(node_handle);
  }

  auto OnNodeDestroyed(const NodeHandle& node_handle) noexcept -> void override
  {
    node_destroyed.push_back(node_handle);
  }

  auto OnLightChanged(const NodeHandle& node_handle) noexcept -> void override
  {
    light_changed.push_back(node_handle);
  }

  auto OnCameraChanged(const NodeHandle& node_handle) noexcept -> void override
  {
    camera_changed.push_back(node_handle);
  }

  std::vector<NodeHandle> transform_changed {};
  std::vector<NodeHandle> node_destroyed {};
  std::vector<NodeHandle> light_changed {};
  std::vector<NodeHandle> camera_changed {};
};

class NoopScriptSlotProcessor final
  : public oxygen::scene::internal::IScriptSlotMutationProcessor {
public:
  auto Process(const oxygen::scene::internal::ScriptSlotMutation& /*mutation*/,
    const ResolveScriptSlotFn& /*resolve_slot*/,
    const NotifyObserversFn& /*notify_observers*/) -> void override
  {
  }

  auto QueueTrackedSlotDeactivations(
    oxygen::scene::internal::IMutationCollector& /*mutation_collector*/) const
    -> void override
  {
  }
};

inline auto MakeScene(const char* name, const size_t capacity = 100)
  -> std::shared_ptr<Scene>
{
  return std::make_shared<Scene>(name, capacity);
}

} // namespace oxygen::scene::testing_support
