//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::data {
class TextureResource;
} // namespace oxygen::data

namespace oxygen::examples::textured_cube {

//! Service for loading and managing runtime textures.
/*!
 This class handles texture loading from disk, cooking through the import
 pipeline, and uploading to the GPU via the AssetLoader.

 ### Features

 - Loads images from common formats (PNG, JPEG, HDR, EXR)
 - Configurable output format (RGBA8, BC7, RGBA16F, RGBA32F)
 - Optional mipmap generation
 - HDR to LDR tone mapping support

 ### Usage

 ```cpp
 TextureLoadingService loader(asset_loader);
 auto result = co_await loader.LoadTextureAsync(path, options);
 if (result.success) {
   // Use result.resource_key
 }
 ```
*/
class TextureLoadingService final {
public:
  //! Options for texture loading.
  struct LoadOptions {
    //! Output format index: 0=RGBA8, 1=BC7, 2=RGBA16F, 3=RGBA32F.
    int output_format_idx { 0 };

    //! Generate mipmaps.
    bool generate_mips { true };

    //! Tonemap HDR content to LDR.
    bool tonemap_hdr_to_ldr { false };

    //! Exposure adjustment for HDR (in EV).
    float hdr_exposure_ev { 0.0f };
  };

  //! Result of a texture load operation.
  struct LoadResult {
    bool success { false };
    oxygen::content::ResourceKey resource_key { 0U };
    std::string status_message {};
    int width { 0 };
    int height { 0 };
  };

  explicit TextureLoadingService(
    oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader);

  ~TextureLoadingService() = default;

  TextureLoadingService(const TextureLoadingService&) = delete;
  auto operator=(const TextureLoadingService&)
    -> TextureLoadingService& = delete;
  TextureLoadingService(TextureLoadingService&&) = delete;
  auto operator=(TextureLoadingService&&) -> TextureLoadingService& = delete;

  //! Load a texture from file asynchronously.
  auto LoadTextureAsync(const std::string& file_path,
    const LoadOptions& options) -> co::Co<LoadResult>;

  //! Get a fresh synthetic texture key.
  [[nodiscard]] auto MintTextureKey() const -> oxygen::content::ResourceKey;

private:
  oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader_;
};

} // namespace oxygen::examples::textured_cube
