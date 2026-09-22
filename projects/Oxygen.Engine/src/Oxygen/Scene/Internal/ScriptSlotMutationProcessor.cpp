//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include <unordered_set>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Internal/MutationTypes.h>
#include <Oxygen/Scene/Internal/ScriptSlotMutationProcessor.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/ScriptSlotIndex.h>

namespace oxygen::scene::internal {

namespace {

  struct ScriptSlotKey final {
    NodeHandle node_handle;
    ScriptSlotIndex slot_index;

    [[nodiscard]] auto operator==(const ScriptSlotKey&) const noexcept -> bool
      = default;
  };

  struct ScriptSlotKeyHash final {
    [[nodiscard]] auto operator()(const ScriptSlotKey& key) const noexcept
      -> size_t
    {
      size_t seed = std::hash<NodeHandle> {}(key.node_handle);
      oxygen::HashCombine(seed, key.slot_index);
      return seed;
    }
  };

  [[nodiscard]] auto IsScriptSlotActive(const ScriptingComponent::Slot& slot)
    -> bool
  {
    return slot.State() == ScriptingComponent::Slot::CompileState::kReady
      && !slot.IsDisabled() && slot.Executable() != nullptr;
  }

  class ScriptSlotMutationProcessor final
    : public IScriptSlotMutationProcessor {
  public:
    auto Process(const ScriptSlotMutation& mutation,
      const ResolveScriptSlotFn& resolve_slot,
      const NotifyObserversFn& notify_observers) -> void override
    {
      const ScriptSlotKey key {
        .node_handle = mutation.node_handle,
        .slot_index = mutation.slot_index,
      };

      switch (mutation.type) {
      case ScriptSlotMutationType::kActivated:
      case ScriptSlotMutationType::kChanged: {
        const auto* slot = resolve_slot(key.node_handle, key.slot_index);
        if (slot == nullptr || !IsScriptSlotActive(*slot)) {
          if (synced_script_slots_.erase(key) > 0) {
            notify_observers(SceneMutationMask::kScriptSlotDeactivated,
              key.node_handle, key.slot_index, nullptr);
          }
          break;
        }

        const auto inserted = synced_script_slots_.insert(key).second;
        if (inserted) {
          notify_observers(SceneMutationMask::kScriptSlotActivated,
            key.node_handle, key.slot_index, slot);
        } else if (mutation.type == ScriptSlotMutationType::kChanged) {
          // MarkSlotReady records only actual executable changes. Comparing
          // against the live slot here loses a queued change when an earlier
          // activation observes that same final executable during this drain.
          notify_observers(SceneMutationMask::kScriptSlotChanged,
            key.node_handle, key.slot_index, slot);
        }
        break;
      }
      case ScriptSlotMutationType::kDeactivated: {
        if (synced_script_slots_.erase(key) > 0) {
          notify_observers(SceneMutationMask::kScriptSlotDeactivated,
            key.node_handle, key.slot_index, nullptr);
        }
        break;
      }
      }
    }

    auto QueueTrackedSlotDeactivations(
      IMutationCollector& mutation_collector) const -> void override
    {
      for (const auto& key : synced_script_slots_) {
        mutation_collector.CollectScriptSlotDeactivated(
          key.node_handle, key.slot_index);
      }
    }

  private:
    std::unordered_set<ScriptSlotKey, ScriptSlotKeyHash> synced_script_slots_;
  };

} // namespace

auto CreateScriptSlotMutationProcessor()
  -> std::unique_ptr<IScriptSlotMutationProcessor>
{
  return std::make_unique<ScriptSlotMutationProcessor>();
}

} // namespace oxygen::scene::internal
