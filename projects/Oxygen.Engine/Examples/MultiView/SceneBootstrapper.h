//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::multiview {

//! Builds and maintains the sample scene graph for the MultiView demo.
class SceneBootstrapper {
public:
  SceneBootstrapper() = default;
  SceneBootstrapper(const SceneBootstrapper&) = delete;
  SceneBootstrapper(SceneBootstrapper&&) = delete;
  auto operator=(const SceneBootstrapper&) -> SceneBootstrapper& = delete;
  auto operator=(SceneBootstrapper&&) -> SceneBootstrapper& = delete;
  ~SceneBootstrapper() = default;

  [[nodiscard]] auto EnsureScene() -> std::shared_ptr<scene::Scene>;
  [[nodiscard]] auto EnsureSceneWithContent() -> std::shared_ptr<scene::Scene>;
  [[nodiscard]] auto GetScene() const -> std::shared_ptr<scene::Scene>;
  [[nodiscard]] auto GetSphereNode() const -> scene::SceneNode;

private:
  auto EnsureSphere(scene::Scene& scene) -> void;

  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode sphere_node_;
};

} // namespace oxygen::examples::multiview
