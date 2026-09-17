//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::multiview {

//! Builds and maintains the sample scene graph for the MultiView demo.
/*!
 Creates geometry (Sphere, Cube, Cylinder, Cone) and lighting (key + fill)
 for the multi-view rendering example. The lighting setup follows a classic
 2-point arrangement optimized for product visualization.
*/
class SceneBootstrapper {
public:
  explicit SceneBootstrapper(
    bool point_light_enabled = true, bool spot_light_enabled = true)
    : point_light_enabled_(point_light_enabled)
    , spot_light_enabled_(spot_light_enabled)
  {
  }
  SceneBootstrapper(const SceneBootstrapper&) = delete;
  SceneBootstrapper(SceneBootstrapper&&) = delete;
  auto operator=(const SceneBootstrapper&) -> SceneBootstrapper& = delete;
  auto operator=(SceneBootstrapper&&) -> SceneBootstrapper& = delete;
  ~SceneBootstrapper() = default;

  void BindToScene(observer_ptr<scene::Scene> scene);
  [[nodiscard]] auto EnsureSceneWithContent() -> observer_ptr<scene::Scene>;
  [[nodiscard]] auto GetScene() const -> observer_ptr<scene::Scene>;
  [[nodiscard]] auto GetSphereNode() const -> scene::SceneNode;
  [[nodiscard]] auto GetCubeNode() const -> scene::SceneNode;
  [[nodiscard]] auto GetCylinderNode() const -> scene::SceneNode;
  [[nodiscard]] auto GetConeNode() const -> scene::SceneNode;
  [[nodiscard]] auto GetGroundPlaneNode() const -> scene::SceneNode;
  //! Deterministic deferred/forward/mixed AP fixture for the exposure proof
  //! CLI.
  auto ApplyAtmosphereProof(std::uint64_t frame) -> void;
  //! Sunlit original meshes/materials with ordinary point and spot lights.
  auto ApplyLitAtmosphereProof() -> void;

private:
  auto EnsureProofAtmosphere(
    float sun_lux, float scattering_strength, bool backlit = true) -> void;
  auto EnsureSphere(scene::Scene& scene) -> void;
  auto EnsureCube(scene::Scene& scene) -> void;
  auto EnsureCylinder(scene::Scene& scene) -> void;
  auto EnsureCone(scene::Scene& scene) -> void;
  auto EnsureGroundPlane(scene::Scene& scene) -> void;
  auto EnsureLighting(scene::Scene& scene) -> void;

  observer_ptr<scene::Scene> scene_ { nullptr };
  scene::SceneNode sphere_node_;
  scene::SceneNode cube_node_;
  scene::SceneNode cylinder_node_;
  scene::SceneNode cone_node_;
  scene::SceneNode ground_plane_node_;

  // Lighting nodes
  bool point_light_enabled_ { true };
  bool spot_light_enabled_ { true };
  scene::SceneNode key_light_node_;
  scene::SceneNode fill_light_node_;
  scene::SceneNode proof_sun_node_;
  std::uint32_t atmosphere_proof_phase_ { ~0U };
};

} // namespace oxygen::examples::multiview
