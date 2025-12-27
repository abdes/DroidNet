//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::data {
class TextureResource;
} // namespace oxygen::data

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
  //! Get or allocate by opaque content::ResourceKey (source-aware key)
  /*!
    The renderer MUST use the opaque `content::ResourceKey` form only.
    Index-based allocation paths are removed to prevent duplicate allocations
    and ensure a single authoritative CPU-side loader (`AssetLoader`).
  */
  OXGN_RNDR_API auto GetOrAllocate(content::ResourceKey resource_key)
    -> ShaderVisibleIndex;

#if defined(OXYGEN_ENGINE_TESTING)
  struct DebugEntrySnapshot {
    std::shared_ptr<graphics::Texture> texture;
    std::shared_ptr<graphics::Texture> placeholder_texture;
    bindless::HeapIndex descriptor_index { kInvalidBindlessHeapIndex };
    bool is_placeholder { false };
    bool load_failed { false };
    std::optional<std::uint64_t> pending_fence;
  };

  [[nodiscard]] auto DebugGetEntry(content::ResourceKey key) const
    -> std::optional<DebugEntrySnapshot>
  {
    const auto it = texture_map_.find(key);
    if (it == texture_map_.end()) {
      return std::nullopt;
    }

    DebugEntrySnapshot snapshot;
    snapshot.texture = it->second.texture;
    snapshot.placeholder_texture = it->second.placeholder_texture;
    snapshot.descriptor_index = it->second.descriptor_index;
    snapshot.is_placeholder = it->second.is_placeholder;
    snapshot.load_failed = it->second.load_failed;
    if (it->second.pending_ticket.has_value()) {
      snapshot.pending_fence = it->second.pending_ticket->fence.get();
    }
    return snapshot;
  }
#endif

  //! Get error-indicator texture SRV index for loading failures.
  /*!
   Returns the SRV index for the magenta/black checkerboard error texture,
   used only when texture loading/creation fails.

   @return SRV index for error-indicator texture
  */
  [[nodiscard]] OXGN_RNDR_API auto GetErrorTextureIndex() const
    -> ShaderVisibleIndex;

private:
  enum class FailurePolicy {
    kBindErrorTexture,
    kKeepPlaceholderBound,
  };

  struct TextureEntry {
    std::shared_ptr<graphics::Texture> texture;
    std::shared_ptr<graphics::Texture> placeholder_texture;
    ShaderVisibleIndex srv_index;
    bindless::HeapIndex descriptor_index { kInvalidBindlessHeapIndex };
    bool is_placeholder { true };
    bool load_failed { false };
    std::optional<oxygen::engine::upload::UploadTicket> pending_ticket;
    std::optional<graphics::TextureViewDescription> pending_view_desc;
    // Optional source-aware resource key when available. Kept opaque to
    // renderer public headers; used to request loads from AssetLoader.
    std::optional<oxygen::content::ResourceKey> resource_key;
  };

  auto CreatePlaceholderTexture() -> std::shared_ptr<graphics::Texture>;
  auto CreateErrorTexture() -> std::shared_ptr<graphics::Texture>;
  auto InitiateAsyncLoad(content::ResourceKey resource_key, TextureEntry& entry)
    -> void;

  auto OnTextureResourceLoaded(content::ResourceKey resource_key,
    std::shared_ptr<data::TextureResource> tex_res) -> void;

  auto HandleLoadFailure(content::ResourceKey resource_key, TextureEntry& entry,
    FailurePolicy policy,
    std::shared_ptr<graphics::Texture>&& texture_to_release) -> void;

  auto TryRepointEntryToErrorTexture(
    content::ResourceKey resource_key, TextureEntry& entry) -> bool;

  auto ReleaseEntryPlaceholderIfOwned(TextureEntry& entry) -> void;

  auto FindEntryOrLog(content::ResourceKey resource_key) -> TextureEntry*;

  auto SubmitTextureUpload(content::ResourceKey resource_key,
    TextureEntry& entry, const graphics::TextureDesc& desc,
    std::shared_ptr<graphics::Texture>&& new_texture,
    std::vector<oxygen::engine::upload::UploadSubresource>&& dst_subresources,
    oxygen::engine::upload::UploadTextureSourceView&& src_view,
    std::size_t trailing_bytes) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  observer_ptr<content::AssetLoader> asset_loader_;

  std::unordered_map<content::ResourceKey, TextureEntry> texture_map_;

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
