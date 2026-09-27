//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "Async/AsyncDemoTypes.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::data {
class MaterialAsset;
}
namespace oxygen::examples::async {
//! Owns the procedural demo entities, material snapshots and their animation.
class AsyncScene final {
public:
  AsyncScene() = default;
  ~AsyncScene() = default;
  OXYGEN_MAKE_NON_COPYABLE(AsyncScene)
  OXYGEN_MAKE_NON_MOVABLE(AsyncScene)
  auto Populate(scene::Scene& scene) -> void;
  auto Animate(double elapsed_seconds) -> void;
  auto UpdateMaterials(double elapsed_seconds) -> void;
  [[nodiscard]] auto Spheres() const -> const std::vector<SphereState>&
  {
    return spheres_;
  }
  [[nodiscard]] auto Camera() const -> scene::SceneNode { return main_camera_; }
  [[nodiscard]] auto Sun() const -> scene::SceneNode { return sun_light_; }

private:
  static auto PopulateEnvironment(scene::Scene& scene) -> void;
  std::vector<SphereState> spheres_;
  scene::SceneNode multisubmesh_;
  scene::SceneNode main_camera_;
  scene::SceneNode sun_light_;
  scene::SceneNode hero_;
  std::shared_ptr<const data::MaterialAsset> blue_override_;
  int last_vis_toggle_ { -1 };
  int last_ovr_toggle_ { -1 };
};
} // namespace oxygen::examples::async
