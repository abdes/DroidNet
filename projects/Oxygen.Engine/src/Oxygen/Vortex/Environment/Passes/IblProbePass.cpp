//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Environment/Types/StaticSkyLightProducts.h>

namespace oxygen::vortex::environment {

namespace {

  auto ProbeBindingsHaveUsableResources(const EnvironmentProbeBindings& probes)
    -> bool
  {
    return probes.environment_map_srv.IsValid()
      && probes.diffuse_sh_srv.IsValid() && probes.probe_revision != 0U;
  }

  auto ResetProbeResourceSlots(EnvironmentProbeBindings& probes) -> void
  {
    probes.environment_map_srv = kInvalidShaderVisibleIndex;
    probes.diffuse_sh_srv = kInvalidShaderVisibleIndex;
    probes.irradiance_map_srv = kInvalidShaderVisibleIndex;
    probes.prefiltered_map_srv = kInvalidShaderVisibleIndex;
  }

} // namespace

auto IblProbePass::Refresh(const EnvironmentProbeState& current_state,
  const bool environment_source_changed) const -> RefreshState
{
  auto next_state = current_state;
  if (environment_source_changed) {
    next_state.probes.probe_revision += 1U;
    ResetProbeResourceSlots(next_state.probes);
    next_state.static_sky_light.status
      = StaticSkyLightProductStatus::kUnavailable;
    next_state.static_sky_light.unavailable_reason
      = StaticSkyLightUnavailableReason::kProcessingFailed;
    next_state.valid = false;
    next_state.flags
      = kEnvironmentProbeStateFlagUnavailable | kEnvironmentProbeStateFlagStale;
  } else if (next_state.valid
    && ProbeBindingsHaveUsableResources(next_state.probes)) {
    next_state.flags = kEnvironmentProbeStateFlagResourcesValid;
  } else {
    next_state.valid = false;
    next_state.flags = kEnvironmentProbeStateFlagUnavailable;
  }

  return {
    .requested = environment_source_changed,
    .refreshed = environment_source_changed,
    .probe_state = next_state,
  };
}

} // namespace oxygen::vortex::environment
