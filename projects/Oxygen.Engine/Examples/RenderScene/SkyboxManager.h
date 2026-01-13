//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <glm/glm.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::examples::render_scene {

//! Manages skybox loading and scene environment configuration.
/*!\
 This helper supports loading a skybox image from disk (HDR/EXR or LDR), cooking
 it into a cubemap `TextureResource` payload, and publishing it through the
 engine `AssetLoader` using a synthetic `ResourceKey`.

 Once loaded, the cubemap can be applied to the scene environment via
 `SkySphere` (background) and `SkyLight` (IBL).

 This exists in the RenderScene example because cooked scene assets currently
 store environment cubemap references as `AssetKey` values, while runtime
 environment systems consume `content::ResourceKey`. Loading from disk provides
 a reliable path for exercising the IBL pipeline in the example.
*/
class SkyboxManager final {
public:
  //! Layout of the input skybox image.
  enum class Layout : int {
    kEquirectangular = 0, //!< 2:1 panorama
    kHorizontalCross = 1, //!< 4x3 cross layout
    kVerticalCross = 2, //!< 3x4 cross layout
    kHorizontalStrip = 3, //!< 6x1 strip
    kVerticalStrip = 4, //!< 1x6 strip
  };

  //! Output format for the skybox cubemap.
  enum class OutputFormat : int {
    kRGBA8 = 0, //!< LDR 8-bit
    kRGBA16Float = 1, //!< HDR 16-bit float
    kRGBA32Float = 2, //!< HDR 32-bit float
    kBC7 = 3, //!< BC7 compressed (LDR)
  };

  //! Options for skybox loading.
  struct LoadOptions {
    Layout layout { Layout::kEquirectangular };
    OutputFormat output_format { OutputFormat::kRGBA8 };
    int cube_face_size { 512 };
    bool flip_y { false };

    // HDR handling: required when cooking HDR sources to LDR formats.
    bool tonemap_hdr_to_ldr { false };
    float hdr_exposure_ev { 0.0F };
  };

  //! Sky light parameters used when applying the skybox.
  struct SkyLightParams {
    float intensity { 1.0F };
    float diffuse_intensity { 1.0F };
    float specular_intensity { 1.0F };
    glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  };

  //! Result of a skybox load operation.
  struct LoadResult {
    bool success { false };
    oxygen::content::ResourceKey resource_key { 0U };
    std::string status_message {};
    int face_size { 0 };
  };

  SkyboxManager(oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader,
    std::shared_ptr<scene::Scene> scene);

  ~SkyboxManager() = default;

  SkyboxManager(const SkyboxManager&) = delete;
  auto operator=(const SkyboxManager&) -> SkyboxManager& = delete;
  SkyboxManager(SkyboxManager&&) = delete;
  auto operator=(SkyboxManager&&) -> SkyboxManager& = delete;

  //! Load a skybox from file asynchronously.
  auto LoadSkyboxAsync(const std::string& file_path, const LoadOptions& options)
    -> co::Co<LoadResult>;

  //! Apply loaded skybox to the scene environment.
  auto ApplyToScene(const SkyLightParams& params) -> void;

  //! Update sky light parameters on the current environment.
  auto UpdateSkyLightParams(const SkyLightParams& params) -> void;

  //! Get the current skybox resource key.
  [[nodiscard]] auto GetCurrentResourceKey() const
    -> oxygen::content::ResourceKey
  {
    return current_resource_key_;
  }

private:
  oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader_;
  std::shared_ptr<scene::Scene> scene_;
  oxygen::content::ResourceKey current_resource_key_ { 0U };
};

} // namespace oxygen::examples::render_scene
