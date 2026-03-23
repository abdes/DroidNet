//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>

#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Renderer/Types/SyntheticSunData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::engine::internal {

//! Resolves the effective sun for a view from scene state and lights.
OXGN_RNDR_API auto ResolveSunForView(scene::Scene& scene,
  std::span<const DirectionalLightBasic> directional_lights)
  -> SyntheticSunData;

//! Returns the first sun-tagged directional light, if any.
OXGN_RNDR_API auto FindSunTaggedDirectionalLight(
  std::span<const DirectionalLightBasic> directional_lights)
  -> std::optional<DirectionalLightBasic>;

//! Validates that the resolved sun payload matches the authoritative
//! sun-tagged directional light for direction and illuminance.
OXGN_RNDR_API auto ResolvedSunMatchesDirectionalLight(
  const SyntheticSunData& resolved_sun, const DirectionalLightBasic& light,
  float direction_epsilon = 1.0e-3F,
  float illuminance_relative_epsilon = 1.0e-3F) noexcept -> bool;

} // namespace oxygen::engine::internal
