//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::data {
class TextureResource;
}

namespace oxygen::vortex::environment {

class IblProbePass {
public:
  struct RefreshState {
    bool requested { false };
    bool refreshed { false };
    EnvironmentProbeState probe_state {};
  };

  OXGN_VRTX_API IblProbePass() = default;
  OXGN_VRTX_API ~IblProbePass() = default;

  [[nodiscard]] OXGN_VRTX_API auto Refresh(
    const EnvironmentProbeState& current_state,
    bool environment_source_changed) const -> RefreshState;
  [[nodiscard]] OXGN_VRTX_API auto RefreshStaticSkyLight(
    const EnvironmentProbeState& current_state,
    const SkyLightEnvironmentModel& sky_light) const -> RefreshState;
  [[nodiscard]] OXGN_VRTX_API auto RefreshStaticSkyLight(
    const EnvironmentProbeState& current_state,
    const SkyLightEnvironmentModel& sky_light,
    const data::TextureResource* source_cubemap) const -> RefreshState;
};

} // namespace oxygen::vortex::environment
