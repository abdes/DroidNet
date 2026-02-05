//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Services/SkyboxService.h"
#include "TexturedCube/TextureLoadingService.h"

namespace oxygen::engine {
class Renderer;
}

namespace oxygen::examples::textured_cube {

//! Texture index mode for the sample materials.
enum class TextureIndexMode : std::uint8_t {
  kFallback = 0,
  kForcedError = 1,
  kCustom = 2,
};

//! Texture state for a single sample object.
struct ObjectTextureState {
  TextureIndexMode mode { TextureIndexMode::kFallback };
  std::uint32_t resource_index { 0U };
  oxygen::content::ResourceKey resource_key { 0U };
};

//! Material surface parameters for the sample objects.
struct SurfaceParams {
  float metalness { 0.0F };
  float roughness { 0.5F };
  glm::vec4 base_color { 1.0F };
  bool disable_texture_sampling { false };
};

//! Manages scene setup including cube and demo-specific lights.
/*!
 This class encapsulates the creation and configuration of the demo scene:
 - Cube and Sphere geometry and materials
 - Demo-specific fill light

 All global environment settings (Sun, Sky, etc.) are managed by the DemoShell
 standard panels and services.
*/
class SceneSetup final {
public:
  explicit SceneSetup(observer_ptr<scene::Scene> scene,
    TextureLoadingService& texture_service, SkyboxService& skybox_service,
    std::filesystem::path cooked_root);

  ~SceneSetup() = default;

  OXYGEN_MAKE_NON_COPYABLE(SceneSetup);
  OXYGEN_MAKE_NON_MOVABLE(SceneSetup);

  //! Initialize the scene nodes and demo-specific lighting.
  auto Initialize() -> void;

  //! Update sphere material (geometry is created once; material is overridden).
  auto UpdateSphere(const ObjectTextureState& sphere_texture,
    const SurfaceParams& surface, content::ResourceKey forced_error_key)
    -> void;

  //! Update cube material (geometry is created once; material is overridden).
  auto UpdateCube(const ObjectTextureState& cube_texture,
    const SurfaceParams& surface, content::ResourceKey forced_error_key)
    -> void;

  //! Apply UV transform overrides to the current sample materials.
  auto UpdateUvTransform(engine::Renderer& renderer, const glm::vec2& scale,
    const glm::vec2& offset) -> void;

private:
  // Light node is an implementation detail; configuration lives in .cpp
  auto EnsureNodes() -> void;
  auto EnsureFillLight() -> void;

  observer_ptr<TextureLoadingService> texture_service_;
  observer_ptr<SkyboxService> skybox_service_;
  std::filesystem::path cooked_root_;

  observer_ptr<scene::Scene> scene_ { nullptr };
  scene::SceneNode sphere_node_;
  scene::SceneNode cube_node_;
  scene::SceneNode fill_light_node_;

  std::shared_ptr<const data::MaterialAsset> sphere_material_;
  std::shared_ptr<const data::MaterialAsset> cube_material_;
  std::shared_ptr<data::GeometryAsset> sphere_geometry_;
  std::shared_ptr<data::GeometryAsset> cube_geometry_;
};

} // namespace oxygen::examples::textured_cube
