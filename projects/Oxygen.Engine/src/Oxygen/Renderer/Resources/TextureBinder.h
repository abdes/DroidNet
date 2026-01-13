//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#if defined(OXYGEN_ENGINE_TESTING)
#  include <optional>
#endif

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine::upload {
class StagingProvider;
class UploadCoordinator;
} // namespace oxygen::engine::upload

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::data {
class TextureResource;
} // namespace oxygen::data

namespace oxygen::renderer::resources {

//! Manages texture binding to shader-visible descriptor heap indices.
/*!
 TextureBinder provides runtime texture loading and binding for PBR material
 rendering. It allows materials to reference textures that may be loaded from
 PAK files, loose cooked files, or in-memory buffers, via the `AssetLoader`
 abstraction.

 ### Primary behaviors

 - **Stable SRV indices**: `GetOrAllocate()` returns a stable shader-visible SRV
   index immediately. The SRV index is the value materials use in shaders and
   must remain stable for the lifetime of the entry.
 - **Descriptor repointing model**: The implementation separates the shader
   visible SRV index from the descriptor backing that index. When a per-entry
   descriptor exists, the binder may `UpdateView` on that descriptor to point it
   at a new `Texture` while keeping the same SRV index. This enables transparent
   replacement of placeholder textures with final textures.

 ### Placeholder / error strategy

 The binder uses three distinct cases by design:
 - **Global placeholder (fast fallback)**: a single shared placeholder created
   in the constructor and used for the hot fast-path (e.g., opaque ResourceKey
   `0`). This path does not allocate per-entry descriptors and therefore cannot
   be transparently repointed per-entry.
 - **Per-entry placeholder (temporary, re-pointable)**: on normal allocation the
   binder creates a per-entry placeholder texture and a descriptor view for that
   entry. When the real texture finishes uploading the binder updates the
   entry's descriptor to reference the final texture. The SRV index returned to
   callers remains unchanged while the descriptor is repointed.
 - **Shared error texture (single sink)**: a single magenta/black error texture
   is created once and reused for all failures. Entries may be repointed to this
   shared error texture; the error texture itself is not recreated per-entry.

 These choices balance hot-path performance, predictable SRV indices, and
 transparent in-place replacement when desired.

 ### Failure policies

 The binder supports distinct failure behaviors (see `FailurePolicy`) such as
 binding the shared error texture immediately or keeping the per-entry
 placeholder bound when upload submission failed.

 ### Lifecycle (concise)

 1. `OnFrameStart()` — begin frame; drain upload completions.
 2. `GetOrAllocate()` — return stable SRV index, create per-entry state when
    appropriate, and initiate async load.
 3. Async upload completes — descriptor is updated or repointed; entry state
    transitions accordingly.

 @note Resource key `ResourceKey::kPlaceholder` is a valid, reserved fallback
   index used by the asset pipeline and fast-path code.
 @see MaterialBinder for integration example
 @see ResourceRegistry for stable index pattern
*/
class TextureBinder : public IResourceBinder {
public:
  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::UploadCoordinator;

  OXGN_RNDR_API TextureBinder(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> staging_provider,
    observer_ptr<CoordinatorT> uploader,
    observer_ptr<content::IAssetLoader> texture_loader);

  OXYGEN_MAKE_NON_COPYABLE(TextureBinder)
  OXYGEN_MAKE_NON_MOVABLE(TextureBinder)

  OXGN_RNDR_API ~TextureBinder() override;

  //! Must be called once per frame before any GetOrAllocate() calls.
  OXGN_RNDR_API auto OnFrameStart() -> void;

  //! Must be called once per frame after all rendering.
  OXGN_RNDR_API auto OnFrameEnd() -> void;

  OXGN_RNDR_NDAPI auto GetOrAllocate(const content::ResourceKey& resource_key)
    -> ShaderVisibleIndex override;

  OXGN_RNDR_NDAPI auto IsResourceReady(
    const content::ResourceKey& key) const noexcept -> bool override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::renderer::resources
