//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Renderer/Types/EnvironmentDynamicData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::engine::internal {

//! Resolves the effective sun for a view from scene state and lights.
OXGN_RNDR_API auto ResolveSunForView(scene::Scene& scene,
  std::span<const DirectionalLightBasic> directional_lights)
  -> SyntheticSunData;

} // namespace oxygen::engine::internal
