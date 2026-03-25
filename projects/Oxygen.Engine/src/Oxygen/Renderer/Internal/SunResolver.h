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

//! Resolves the renderer sun for a view.
/*!
 The renderer has exactly one authoritative sun-selection rule:

 - A sun is a directional light published through `DirectionalLightBasic`
   with `DirectionalLightFlags::kSunLight` set.
 - If no directional light is tagged as sun, there is no sun for the view.
 - If more than one directional light is tagged as sun, the scene is invalid
   for renderer sun selection and the resolver returns `kNoSun`.

 The renderer intentionally does not invent a sun from heuristics such as
 "first directional light" and does not treat authored environment-sun data as
 a second authority for direct lighting. Hydrators may still choose to create a
 real directional-light scene node and mark it as sun before the renderer runs.
 `SyntheticSunData` is therefore a derived transport payload, not the source of
 truth.
*/
OXGN_RNDR_API auto ResolveSunForView(scene::Scene& scene,
  std::span<const DirectionalLightBasic> directional_lights)
  -> SyntheticSunData;

//! Returns the unique sun-tagged directional light, if exactly one exists.
/*!
 Returns `std::nullopt` when no sun-tagged directional light exists or when the
 scene is invalid because multiple directional lights are tagged as sun.
*/
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
