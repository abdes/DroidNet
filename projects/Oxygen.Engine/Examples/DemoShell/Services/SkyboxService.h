//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::examples {

//! Manages skybox loading and scene environment configuration.
/*!
 This class handles:
 - Loading skybox images from various layouts (equirectangular, cross, strip)
 - Converting to cubemap format
 - Configuring scene environment with sky sphere and sky lighting

 ### Supported Layouts

 - Equirectangular (2:1 panorama)
 - Horizontal Cross (4x3)
 - Vertical Cross (3x4)
 - Horizontal Strip (6x1)
 - Vertical Strip (1x6)

 ### Usage

 ```cpp
 SkyboxService service(asset_loader, scene);
 SkyboxService::SkyLightParams params {};
 service.LoadAndEquip(path, options, params,
   [&](SkyboxService::LoadResult result) {
     // update UI
   });
 ```
*/
class SkyboxService final {
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

  //! Sky lighting parameters.
  struct SkyLightParams {
    float sky_sphere_intensity { 1.0F };
    float intensity { 1.0F };
    float diffuse_intensity { 1.0F };
    float specular_intensity { 1.0F };
    glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  };

  //! Result of a skybox load operation.
  struct LoadResult {
    bool success { false };
    content::ResourceKey resource_key { 0U };
    std::string status_message {};
    int face_size { 0 };
    //! Estimated sun direction (if detectable from the skybox).
    glm::vec3 estimated_sun_dir { 0.35F, -0.45F, -1.0F };
    bool sun_dir_valid { false };
  };

  using LoadCallback = std::function<void(LoadResult)>;

  SkyboxService(observer_ptr<content::IAssetLoader> asset_loader,
    observer_ptr<scene::Scene> scene);

  ~SkyboxService() = default;

  OXYGEN_MAKE_NON_COPYABLE(SkyboxService)
  OXYGEN_DEFAULT_MOVABLE(SkyboxService)

  //! Begin loading a skybox and invoke `on_complete` when finished.
  auto StartLoadSkybox(const std::string& file_path, const LoadOptions& options,
    LoadCallback on_complete) -> void;

  //! Load a skybox and apply it to the scene upon success.
  auto LoadAndEquip(const std::string& file_path, const LoadOptions& options,
    const SkyLightParams& params, LoadCallback on_complete) -> void;

  //! Set the skybox resource key directly (e.g., from cooked content).
  auto SetSkyboxResourceKey(content::ResourceKey key) -> void;

  //! Apply loaded skybox to the scene environment.
  auto ApplyToScene(const SkyLightParams& params) -> void;

  //! Update sky light parameters on the current environment.
  auto UpdateSkyLightParams(const SkyLightParams& params) -> void;

  //! Get the current skybox resource key.
  [[nodiscard]] auto GetCurrentResourceKey() const -> content::ResourceKey
  {
    return current_resource_key_;
  }

private:
  observer_ptr<content::IAssetLoader> asset_loader_;
  observer_ptr<scene::Scene> scene_ { nullptr };
  content::ResourceKey current_resource_key_ { 0U };

  //! Cached RGBA8 pixel data for sun direction estimation.
  std::vector<std::byte> cached_rgba8_;
  std::uint32_t cached_width_ { 0U };
  std::uint32_t cached_height_ { 0U };
};

} // namespace oxygen::examples
