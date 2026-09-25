//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/vec3.hpp>

#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::environment {
struct AtmosphereModel;
namespace internal {
  struct StableAtmosphereState;

  //! Fixed scene-global origin, one metre above the authored planet's +Z pole.
  [[nodiscard]] OXGN_VRTX_API auto ResolveSkyCaptureOrigin(
    const AtmosphereModel& atmosphere) -> glm::vec3;

  //! Shared native tangent frame and light payloads for visible and captured
  //! sky.
  [[nodiscard]] OXGN_VRTX_API auto BuildAtmosphereViewData(
    const StableAtmosphereState& stable_state,
    const AtmosphereLutCache::InternalParameters& internal_parameters,
    glm::vec3 origin, bool with_height_fog, bool reflection_capture)
    -> EnvironmentViewData;
} // namespace internal
} // namespace oxygen::vortex::environment
