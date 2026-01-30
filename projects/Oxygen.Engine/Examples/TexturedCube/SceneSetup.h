//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "DemoShell/Services/SkyboxService.h"
#include "TexturedCube/TextureLoadingService.h"
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <filesystem>

namespace oxygen::examples::textured_cube {

//! Manages scene setup including cube, lights, and environment.
/*!
 This class encapsulates the creation and configuration of the demo scene:
 - Cube geometry and material
 - Directional (sun) light and fill light
 - Scene environment (sky sphere, sky light)

 ### Usage

 ```cpp
 SceneSetup setup(scene);
 setup.EnsureCubeNode();
 setup.EnsureLighting(sun_params, fill_params);
 setup.EnsureEnvironment(env_params);
 ```
*/
class SceneSetup final {
public:
  //! Sun light parameters.
  struct SunLightParams {
    float intensity { 12.0f };
    glm::vec3 color_rgb { 1.0f, 0.98f, 0.95f };
    glm::vec3 ray_direction { 0.35f, -0.45f, -1.0f };
    bool use_custom_direction { false };
  };

  //! Fill light parameters.
  struct FillLightParams {
    glm::vec3 position { -6.0f, 5.0f, 3.0f };
    float intensity { 80.0f };
    glm::vec3 color_rgb { 0.85f, 0.90f, 1.0f };
    float range { 45.0f };
  };

  //! Environment parameters.
  struct EnvironmentParams {
    glm::vec3 solid_sky_color { 0.06f, 0.08f, 0.12f };
    float sky_intensity { 1.0f };
    float sky_light_intensity { 1.0f };
    float sky_light_diffuse { 1.0f };
    float sky_light_specular { 1.0f };
  };

  //! Texture index mode for the cube material.
  enum class TextureIndexMode : std::uint8_t {
    kFallback = 0,
    kForcedError = 1,
    kCustom = 2,
  };

  explicit SceneSetup(observer_ptr<scene::Scene> scene,
    TextureLoadingService& texture_service, SkyboxService& skybox_service,
    const std::filesystem::path& cooked_root);

  ~SceneSetup() = default;

  SceneSetup(const SceneSetup&) = delete;
  auto operator=(const SceneSetup&) -> SceneSetup& = delete;
  SceneSetup(SceneSetup&&) = delete;
  auto operator=(SceneSetup&&) -> SceneSetup& = delete;

  //! Ensure the cube node exists in the scene.
  auto EnsureCubeNode() -> scene::SceneNode;

  //! Rebuild the cube and sphere geometry with new material/UV settings.
  /*!
   @note TODO: When MaterialAsset baseline UV transforms are fully wired into
         rendering, use those defaults here and move runtime edits to
         per-instance overrides (MaterialInstance).
  */
  auto RebuildCube(TextureIndexMode sphere_texture_mode,
    std::uint32_t sphere_resource_index,
    oxygen::content::ResourceKey sphere_texture_key,
    TextureIndexMode cube_texture_mode, std::uint32_t cube_resource_index,
    oxygen::content::ResourceKey cube_texture_key,
    oxygen::content::ResourceKey forced_error_key, glm::vec2 uv_scale,
    glm::vec2 uv_offset, float metalness, float roughness,
    glm::vec4 base_color_rgba, bool disable_texture_sampling)
    -> std::shared_ptr<const oxygen::data::MaterialAsset>;

  //! Ensure lighting nodes exist and are configured.
  auto EnsureLighting(const SunLightParams& sun, const FillLightParams& fill)
    -> void;

  //! Update sun light parameters.
  auto UpdateSunLight(const SunLightParams& params) -> void;

  //! Ensure the scene environment is configured.
  auto EnsureEnvironment(const EnvironmentParams& params) -> void;

  //! Get the current cube material.
  [[nodiscard]] auto GetCubeMaterial() const
    -> std::shared_ptr<const oxygen::data::MaterialAsset>
  {
    return cube_material_;
  }

  //! Get the current sphere material.
  [[nodiscard]] auto GetSphereMaterial() const
    -> std::shared_ptr<const oxygen::data::MaterialAsset>
  {
    return sphere_material_;
  }

  //! Get the cube node.
  [[nodiscard]] auto GetCubeNode() const -> scene::SceneNode
  {
    return cube_node_;
  }

  //! Get the sun node.
  [[nodiscard]] auto GetSunNode() const -> scene::SceneNode
  {
    return sun_node_;
  }

  //! Retire the current geometry (for deferred cleanup).
  auto RetireCurrentGeometry() -> void;

  //! Cleanup old retired geometries (keep only the most recent N).
  auto CleanupRetiredGeometries(std::size_t max_keep = 16) -> void;

private:
  observer_ptr<scene::Scene> scene_ { nullptr };
  observer_ptr<TextureLoadingService> texture_service_;
  observer_ptr<SkyboxService> skybox_service_;
  std::filesystem::path cooked_root_;

  scene::SceneNode cube_node_;
  scene::SceneNode comparison_cube_node_;
  scene::SceneNode sun_node_;
  scene::SceneNode fill_light_node_;

  std::shared_ptr<const oxygen::data::MaterialAsset> sphere_material_;
  std::shared_ptr<const oxygen::data::MaterialAsset> cube_material_;
  std::shared_ptr<oxygen::data::GeometryAsset> cube_geometry_;
  std::shared_ptr<oxygen::data::GeometryAsset> comparison_cube_geometry_;
  std::vector<std::shared_ptr<oxygen::data::GeometryAsset>>
    retired_cube_geometries_;
};

} // namespace oxygen::examples::textured_cube
