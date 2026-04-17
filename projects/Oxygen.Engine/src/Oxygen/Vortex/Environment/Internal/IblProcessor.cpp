//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>

#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>

namespace oxygen::vortex::environment::internal {

IblProcessor::IblProcessor(Renderer& renderer)
  : renderer_(renderer)
  , probe_pass_(std::make_unique<environment::IblProbePass>())
{
}

IblProcessor::~IblProcessor() = default;

auto IblProcessor::RefreshPersistentProbes(
  const EnvironmentProbeState& current_state, const bool environment_source_changed) const
  -> RefreshState
{
  static_cast<void>(renderer_);
  const auto refreshed = probe_pass_->Refresh(current_state, environment_source_changed);
  return {
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .probe_state = refreshed.probe_state,
  };
}

} // namespace oxygen::vortex::environment::internal
