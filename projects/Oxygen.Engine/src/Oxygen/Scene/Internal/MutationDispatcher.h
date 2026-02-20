//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Internal/MutationTraits.h>
#include <Oxygen/Scene/Internal/ScriptSlotMutationProcessor.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::internal {

class IMutationCollector;

class IMutationDispatcher {
public:
  struct Counters final {
    uint64_t sync_calls { 0 };
    uint64_t frames_with_mutations { 0 };
    uint64_t drained_records { 0 };
    uint64_t script_records_dispatched { 0 };
    uint64_t light_records_coalesced_in { 0 };
    uint64_t light_records_dispatched { 0 };
    uint64_t camera_records_coalesced_in { 0 };
    uint64_t camera_records_dispatched { 0 };
  };

  virtual ~IMutationDispatcher() = default;

  using ResolveScriptSlotFn = IScriptSlotMutationProcessor::ResolveScriptSlotFn;
  using NotifyScriptObserversFn
    = IScriptSlotMutationProcessor::NotifyObserversFn;
  using NotifyLightMutationFn = std::function<void(const LightMutation&)>;
  using NotifyCameraMutationFn = std::function<void(const CameraMutation&)>;

  struct DispatchContext final {
    ResolveScriptSlotFn resolve_script_slot;
    NotifyScriptObserversFn notify_script_observers;
    NotifyLightMutationFn notify_light_mutation;
    NotifyCameraMutationFn notify_camera_mutation;
  };

  virtual auto Dispatch(IMutationCollector& mutation_collector,
    const DispatchContext& context) -> void
    = 0;
  [[nodiscard]] virtual auto GetCounters() const noexcept -> Counters = 0;
};

OXGN_SCN_NDAPI auto CreateMutationDispatcher(
  observer_ptr<IScriptSlotMutationProcessor> script_slot_processor)
  -> std::unique_ptr<IMutationDispatcher>;

} // namespace oxygen::scene::internal
