//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Scene/Internal/MutationTypes.h>

namespace oxygen::scene::internal {

class IMutationCollector {
public:
  virtual ~IMutationCollector() = default;
  virtual auto SetEnabled(bool enabled) -> void = 0;
  [[nodiscard]] virtual auto IsEnabled() const noexcept -> bool = 0;
  virtual auto ClearMutations() -> void = 0;

  virtual auto CollectScriptSlotActivated(
    const NodeHandle& node_handle, ScriptSlotIndex slot_index) -> void
    = 0;
  virtual auto CollectScriptSlotChanged(
    const NodeHandle& node_handle, ScriptSlotIndex slot_index) -> void
    = 0;
  virtual auto CollectScriptSlotDeactivated(
    const NodeHandle& node_handle, ScriptSlotIndex slot_index) -> void
    = 0;

  virtual auto CollectLightChanged(const NodeHandle& node_handle) -> void = 0;
  virtual auto CollectCameraChanged(const NodeHandle& node_handle) -> void = 0;

  virtual auto DrainMutations() -> std::vector<MutationRecord> = 0;
};

} // namespace oxygen::scene::internal
