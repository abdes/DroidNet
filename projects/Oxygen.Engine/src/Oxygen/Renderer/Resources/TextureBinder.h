//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

//! Manages texture binding to shader-visible descriptor heap indices.
/*!
 TextureBinder provides runtime texture loading and binding for PBR material
 rendering, enabling materials to reference textures that can be sampled in
 shaders. Textures can be loaded from PAK files or loose cooked filesystem.

 ## Key Features:

 - **Storage-agnostic**: Works with PAK files or loose cooked files via
   AssetLoader abstraction
 - **Stable SRV indices**: Same resource index always returns same SRV index
 - **Placeholder-to-final swap**: Immediate response with placeholder, async
   load and seamless replacement
 - **Error handling**: Magenta/black checkerboard for loading failures
 - **Deduplication**: Resource index-based identity ensures efficient GPU
   memory usage

 ## Lifecycle:

 1. `OnFrameStart()` called once per frame before any operations
 2. `GetOrAllocate()` called during material serialization
 3. Returns stable SRV index immediately (may reference placeholder)
 4. Async texture loading and upload occurs in background
 5. `OnFrameEnd()` called at end of frame for cleanup

 ## Index Semantics:

 - Resource index `0` is VALID and reserved for the fallback texture.
   The asset pipeline/packer must store the fallback texture at index `0`.
   The runtime may special-case this index as a fast fallback path.
 - Only `kInvalidBindlessIndex` (0xFFFFFFFF) indicates invalid index
 - `GetOrAllocate()` returns stable SRV index immediately

 @see MaterialBinder for integration example
 @see ResourceRegistry for stable index pattern
*/
class TextureBinder {
public:
  /*!
   @param gfx Graphics backend (must outlive TextureBinder)
   @param uploader Upload coordinator for async texture uploads
   @param staging_provider Staging provider used for placeholder uploads
   @param asset_loader Asset loading interface for texture resources
  */
  OXGN_RNDR_API TextureBinder(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    observer_ptr<engine::upload::StagingProvider> staging_provider,
    observer_ptr<content::AssetLoader> asset_loader);

  OXYGEN_MAKE_NON_COPYABLE(TextureBinder)
  OXYGEN_MAKE_NON_MOVABLE(TextureBinder)

  OXGN_RNDR_API ~TextureBinder();

  //! Must be called once per frame before any GetOrAllocate() calls.
  OXGN_RNDR_API auto OnFrameStart() -> void;

  //! Must be called once per frame after all rendering.
  OXGN_RNDR_API auto OnFrameEnd() -> void;

  //! Provide the optional AssetLoader service when it becomes available.
  OXGN_RNDR_API auto SetAssetLoader(
    observer_ptr<content::AssetLoader> asset_loader) -> void;

  //! Get or allocate shader-visible SRV index for texture resource.
  /*!
   Returns existing SRV index if resource was previously allocated, otherwise
   creates a new entry with placeholder texture and initiates async loading.

   @param resource_index Texture resource index from asset system
   @return Stable SRV index usable in shaders

   ### Performance Characteristics

   - Time Complexity: O(1) for hash lookup
   - Memory: Allocates descriptor and placeholder on first call
   - Async: Actual texture loading occurs asynchronously

   ### Usage Examples

   ```cpp
   auto srv_index =
   texture_binder.GetOrAllocate(material.GetBaseColorTexture());
   // srv_index is immediately usable (references placeholder if loading)
   ```

   @note Same resource_index always returns same SRV index
     @note Resource index `0` is reserved for the fallback texture and may be
       special-cased by the runtime (currently returns the placeholder).
   @see GetErrorTextureIndex
  */
  OXGN_RNDR_API auto GetOrAllocate(data::pak::v1::ResourceIndexT resource_index)
    -> ShaderVisibleIndex;

  //! Get error-indicator texture SRV index for loading failures.
  /*!
   Returns the SRV index for the magenta/black checkerboard error texture,
   used only when texture loading/creation fails.

   @return SRV index for error-indicator texture
  */
  [[nodiscard]] OXGN_RNDR_API auto GetErrorTextureIndex() const
    -> ShaderVisibleIndex;

  //! Replace (or create) a 2D texture for a resource index.
  /*!
   This is a synchronous escape hatch intended for tools and examples.

    TODO: Demo/tooling-only. Replace with a production authoring and/or runtime
    streaming pipeline rather than mutating bindings through TextureBinder.

   The SRV descriptor index returned by GetOrAllocate() remains stable; this
   method repoints that descriptor to a new texture and uploads the provided
   RGBA8 pixel data into it.

   @param resource_index The logical resource id used by materials.
   @param width Texture width in pixels.
   @param height Texture height in pixels.
   @param rgba8_bytes Pixel data in RGBA8 (width*height*4 bytes).
   @param debug_name Friendly name for GPU/debug logging.
   @return true on success; false if the texture could not be created/uploaded.
  */
  OXGN_RNDR_API auto OverrideTexture2DRgba8(
    data::pak::v1::ResourceIndexT resource_index, std::uint32_t width,
    std::uint32_t height, std::span<const std::byte> rgba8_bytes,
    const char* debug_name) -> bool;

private:
  struct TextureEntry {
    std::shared_ptr<graphics::Texture> texture;
    ShaderVisibleIndex srv_index;
    bindless::HeapIndex descriptor_index { kInvalidBindlessHeapIndex };
    bool is_placeholder { true };
    bool load_failed { false };
  };

  auto CreatePlaceholderTexture() -> std::shared_ptr<graphics::Texture>;
  auto CreateErrorTexture() -> std::shared_ptr<graphics::Texture>;
  auto InitiateAsyncLoad(
    data::pak::v1::ResourceIndexT resource_index, TextureEntry& entry) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  observer_ptr<content::AssetLoader> asset_loader_;

  std::unordered_map<data::pak::v1::ResourceIndexT, TextureEntry> texture_map_;

  std::shared_ptr<graphics::Texture> placeholder_texture_;
  ShaderVisibleIndex placeholder_texture_index_ { kInvalidShaderVisibleIndex };
  std::shared_ptr<graphics::Texture> error_texture_;
  ShaderVisibleIndex error_texture_index_ { kInvalidShaderVisibleIndex };

  std::uint64_t total_requests_ { 0U };
  std::uint64_t cache_hits_ { 0U };
  std::uint64_t load_failures_ { 0U };

  auto SubmitTextureData(const std::shared_ptr<graphics::Texture>& texture,
    const std::span<const std::byte> data, const char* debug_name) -> void;
};

} // namespace oxygen::renderer::resources
