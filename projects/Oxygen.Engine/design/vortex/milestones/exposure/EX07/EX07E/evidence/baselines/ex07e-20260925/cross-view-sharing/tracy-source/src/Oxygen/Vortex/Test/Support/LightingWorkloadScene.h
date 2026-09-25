//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkload.h>

namespace oxygen::vortex::testing {

//! Native scene shared by the many-light preview and opt-in baseline.
//! The recipe owns light/camera inputs; this owner supplies real scene nodes,
//! a neutral floor and small raised occluders for requested local shadows.
struct LightingWorkloadScene {
  std::shared_ptr<scene::Scene> scene;
  scene::SceneNode floor;
  std::vector<scene::SceneNode> cameras;
  std::vector<scene::SceneNode> lights;
  std::vector<scene::SceneNode> casters;

  //! Update existing light transforms without replacing their identities.
  auto ApplyMotion(const LightingWorkload& workload) -> void;
};

[[nodiscard]] auto CreateLightingWorkloadScene(const LightingWorkload& workload)
  -> LightingWorkloadScene;

} // namespace oxygen::vortex::testing
