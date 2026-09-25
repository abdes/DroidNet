//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::examples::light_bench {

//! Owns the LightBench scene and its lifecycle.
/*!
 This class is responsible for creating and retaining the demo scene. Scene
 starts with explicit post-process settings and a reference light; demo
 preferences never supply the experiment's initial state.
*/
class LightScene final {
public:
  //! Create a new scene and bind it for subsequent updates.
  auto CreateScene() -> std::unique_ptr<scene::Scene>;

  //! Shared canonical camera construction for the app and its native test.
  static auto CreateReferenceCamera(scene::Scene& scene) -> scene::SceneNode;
  static auto FitReferenceCamera(scene::SceneNode& camera, float aspect)
    -> void;
  //! Restore local geometry/light state before staging a fresh reference scene.
  auto ResetReference() -> void;

  //! Bind an externally owned scene for updates.
  void SetScene(observer_ptr<scene::Scene> scene);

  //! Clear the bound scene reference.
  void ClearScene();

  struct SceneObjectState {
    bool enabled { false };
    Vec3 position { 0.0F, 0.0F, 0.0F };
    Vec3 rotation_deg { 0.0F, 0.0F, 0.0F };
    Vec3 scale { 1.0F, 1.0F, 1.0F };
    auto operator==(const SceneObjectState&) const -> bool = default;
  };

  struct PointLightState {
    bool enabled { false };
    Vec3 position { -3.0F, 3.0F, 3.0F };
    Vec3 color_rgb { 1.0F, 1.0F, 1.0F };
    float intensity { 50.0F };
    float range { 15.0F };
    float source_radius { 0.0F };
    bool casts_shadows { false };
    auto operator==(const PointLightState&) const -> bool = default;
  };

  struct SpotLightState {
    bool enabled { false };
    Vec3 position { 3.0F, -3.0F, 3.0F };
    Vec3 direction_ws { 0.0F, 1.0F, -1.0F };
    Vec3 color_rgb { 1.0F, 1.0F, 1.0F };
    float intensity { 80.0F };
    float range { 20.0F };
    float inner_angle_deg { 20.0F };
    float outer_angle_deg { 30.0F };
    float source_radius { 0.0F };
    bool casts_shadows { false };
    auto operator==(const SpotLightState&) const -> bool = default;
  };

  struct DirectionalLightState {
    bool enabled { true };
    Vec3 color_rgb { 1.0F };
    float illuminance_lux { 1000.0F };
    Vec3 direction_ws { 0.0F, -1.0F, 0.0F };
    bool casts_shadows { false };
    auto operator==(const DirectionalLightState&) const -> bool = default;
  };
  [[nodiscard]] auto GetDirectionalLightState() -> DirectionalLightState&
  {
    return directional_light_state_;
  }

  LightScene();
  explicit LightScene(std::string_view name);

  LightScene(const LightScene&) = delete;
  auto operator=(const LightScene&) -> LightScene& = delete;
  LightScene(LightScene&&) = default;
  auto operator=(LightScene&&) -> LightScene& = default;

  ~LightScene() = default;

  //! Update light nodes to match the current state.
  auto Update() -> void;

  //! Reset a scene object to its default transform/state.
  auto ResetSceneObject(std::string_view label) -> void;

  //! Reset the scene instance.
  auto Reset() -> void;

  //! Access the point light state.
  [[nodiscard]] auto GetPointLightState() -> PointLightState&
  {
    return point_light_state_;
  }

  //! Access the spot light state.
  [[nodiscard]] auto GetSpotLightState() -> SpotLightState&
  {
    return spot_light_state_;
  }

  [[nodiscard]] auto GetGrayCardState() -> SceneObjectState&
  {
    return gray_card_state_;
  }

  [[nodiscard]] auto GetWhiteCardState() -> SceneObjectState&
  {
    return white_card_state_;
  }

  [[nodiscard]] auto GetBlackCardState() -> SceneObjectState&
  {
    return black_card_state_;
  }

  [[nodiscard]] auto GetMatteSphereState() -> SceneObjectState&
  {
    return matte_sphere_state_;
  }

  [[nodiscard]] auto GetGlossySphereState() -> SceneObjectState&
  {
    return glossy_sphere_state_;
  }

  [[nodiscard]] auto GetGroundPlaneState() -> SceneObjectState&
  {
    return ground_plane_state_;
  }

  //! Get the current scene instance (may be null).
  [[nodiscard]] auto GetScene() const -> observer_ptr<scene::Scene>
  {
    return scene_;
  }

private:
  auto EnsureSceneGeometry() -> void;
  auto EnsureGeometryAssets() -> void;
  auto EnsureReferenceNodes() -> void;
  auto ApplySceneObjectState(scene::SceneNode& node,
    const SceneObjectState& state, bool allow_rotation) -> void;
  auto ApplySceneVisibility(scene::SceneNode& node, bool visible) -> void;
  auto ApplySceneTransforms() -> void;

  auto EnsurePointLightNode() -> void;
  auto EnsureSpotLightNode() -> void;
  auto ApplyPointLightState() -> void;
  auto ApplySpotLightState() -> void;

  auto BuildSurfaceGeometry(std::string_view generator, std::string_view name,
    std::shared_ptr<const data::MaterialAsset> material)
    -> std::shared_ptr<const data::GeometryAsset>;
  auto BuildSphereGeometry(
    std::string_view name, std::shared_ptr<const data::MaterialAsset> material)
    -> std::shared_ptr<const data::GeometryAsset>;
  auto MakeSolidColorMaterial(std::string_view name, const Vec4& rgba,
    float roughness, float metalness, bool double_sided)
    -> std::shared_ptr<const data::MaterialAsset>;

  std::string name_;
  observer_ptr<scene::Scene> scene_ { nullptr };
  scene::SceneNode point_light_node_ {};
  scene::SceneNode directional_light_node_ {};
  scene::SceneNode spot_light_node_ {};
  PointLightState point_light_state_ {};
  DirectionalLightState directional_light_state_ {};
  SpotLightState spot_light_state_ {};

  SceneObjectState gray_card_state_ {};
  SceneObjectState white_card_state_ {};
  SceneObjectState black_card_state_ {};
  SceneObjectState matte_sphere_state_ {};
  SceneObjectState glossy_sphere_state_ {};
  SceneObjectState ground_plane_state_ {};

  scene::SceneNode gray_card_node_ {};
  scene::SceneNode white_card_node_ {};
  scene::SceneNode black_card_node_ {};
  scene::SceneNode matte_sphere_node_ {};
  scene::SceneNode glossy_sphere_node_ {};
  scene::SceneNode ground_plane_node_ {};

  std::shared_ptr<const data::GeometryAsset> gray_card_geo_ {};
  std::shared_ptr<const data::GeometryAsset> white_card_geo_ {};
  std::shared_ptr<const data::GeometryAsset> black_card_geo_ {};
  std::shared_ptr<const data::GeometryAsset> matte_sphere_geo_ {};
  std::shared_ptr<const data::GeometryAsset> glossy_sphere_geo_ {};
  std::shared_ptr<const data::GeometryAsset> ground_plane_geo_ {};
};

} // namespace oxygen::examples::light_bench
