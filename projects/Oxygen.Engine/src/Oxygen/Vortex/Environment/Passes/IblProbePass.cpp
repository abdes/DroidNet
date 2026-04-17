//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>

namespace oxygen::vortex::environment {

auto IblProbePass::Refresh(
  const EnvironmentProbeState& current_state, const bool environment_source_changed) const
  -> RefreshState
{
  auto next_state = current_state;
  if (environment_source_changed) {
    next_state.probes.probe_revision += 1U;
  }

  return {
    .requested = environment_source_changed,
    .refreshed = environment_source_changed,
    .probe_state = next_state,
  };
}

} // namespace oxygen::vortex::environment
