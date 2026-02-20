//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <Oxygen/Scene/Internal/MutationTypes.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::scene::internal {

class IMutationCollector;

class IScriptSlotMutationProcessor {
public:
  virtual ~IScriptSlotMutationProcessor() = default;

  using ResolveScriptSlotFn = std::function<const ScriptingComponent::Slot*(
    const NodeHandle& node_handle, ScriptSlotIndex slot_index)>;
  using NotifyObserversFn = std::function<void(SceneMutationMask mutation_type,
    const NodeHandle& node_handle, ScriptSlotIndex slot_index,
    const ScriptingComponent::Slot* slot)>;

  virtual auto Process(const ScriptSlotMutation& mutation,
    const ResolveScriptSlotFn& resolve_slot,
    const NotifyObserversFn& notify_observers) -> void
    = 0;

  virtual auto QueueTrackedSlotDeactivations(
    IMutationCollector& mutation_collector) const -> void
    = 0;
};

[[nodiscard]] auto CreateScriptSlotMutationProcessor()
  -> std::unique_ptr<IScriptSlotMutationProcessor>;

} // namespace oxygen::scene::internal
