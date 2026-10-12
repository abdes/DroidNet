//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Resources/IResourceBinder.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::vortex::upload {
class StagingProvider;
class UploadCoordinator;
} // namespace oxygen::vortex::upload

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::data {
class TextureResource;
} // namespace oxygen::data

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::vortex::resources {

//! Manages texture binding to shader-visible descriptor heap indices.
/*!
 TextureBinder provides runtime texture loading and binding for PBR material
 rendering. It allows materials to reference textures that may be loaded from
 PAK files, loose cooked files, or in-memory buffers, via the `AssetLoader`
 abstraction.

 ### Versioned descriptors

 `GetOrAllocate()` returns the index the entry publishes now. Each texture
 version gets its own descriptor; nothing recorded GPU work can read is
 rewritten. A replaced version, with its descriptor, retires through
 `Graphics::RetireResource` once the frames that may read it complete.
 Consumers read the current index every frame, or re-resolve it when
 `GetResidentContentRevision()` changes.

 An entry publishes, in turn:

 - the shared placeholder while its first load is in flight;
 - its own descriptor once the upload completes;
 - the shared error binding when loading fails;
 - the shared placeholder of its shape (2D, cube or 2D array) once evicted.

 Placeholder and error bindings exist once per shape, so a shader always
 reads a view of the dimension it declares. They live as long as the binder
 and are initialized on the graphics queue when it is constructed, so every
 later pass that samples them runs after their copy.

 ### Entry identity

 Each entry life carries a Nexus `IndexReuse` handle. Async loads, upload
 completions, retries and evictions capture it; eviction retires it, so work
 issued for an earlier load is discarded instead of touching the reloaded
 entry.

 ### Lifecycle (concise)

 1. `OnFrameStart()` — process evictions and publish completed uploads.
 2. `GetOrAllocate()` — return the current index, create the entry when
    needed, and initiate the async load.
 3. `EnsureFrameResources()` — submit queued uploads and deferred retries.

 @note Resource key `ResourceKey::kPlaceholder` is a valid, reserved fallback
   index used by the asset pipeline and fast-path code.
 @see MaterialBinder for integration example
*/
class TextureBinder : public IResourceBinder {
public:
  using ProviderT = vortex::upload::StagingProvider;
  using CoordinatorT = vortex::upload::UploadCoordinator;

  struct UploadLimits {
    static constexpr std::size_t kDefaultMaxUploadBytesPerFrame
      = 128ULL * 1024ULL * 1024ULL;
    static constexpr std::size_t kDefaultMaxPendingUploadBytes
      = 128ULL * 1024ULL * 1024ULL;
    static constexpr std::size_t kDefaultDeferredRetryLowWatermarkBytes
      = 64ULL * 1024ULL * 1024ULL;
    static constexpr std::size_t kDefaultMaxDeferredRetriesPerFrame = 4U;

    std::size_t max_completion_visits_per_frame { 128U };
    std::size_t max_upload_visits_per_frame { 128U };
    std::size_t max_eviction_visits_per_frame { 64U };
    std::size_t max_upload_bytes_per_frame { kDefaultMaxUploadBytesPerFrame };
    std::size_t max_pending_upload_bytes { kDefaultMaxPendingUploadBytes };
    std::size_t deferred_retry_low_watermark_bytes {
      kDefaultDeferredRetryLowWatermarkBytes,
    };
    std::size_t max_deferred_retries_per_frame {
      kDefaultMaxDeferredRetriesPerFrame,
    };
  };

  OXGN_VRTX_API TextureBinder(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> staging_provider,
    observer_ptr<CoordinatorT> uploader,
    observer_ptr<content::IAssetLoader> texture_loader, UploadLimits limits);

  OXGN_VRTX_API TextureBinder(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> staging_provider,
    observer_ptr<CoordinatorT> uploader,
    observer_ptr<content::IAssetLoader> texture_loader);

  OXYGEN_MAKE_NON_COPYABLE(TextureBinder)
  OXYGEN_MAKE_NON_MOVABLE(TextureBinder)

  OXGN_VRTX_API ~TextureBinder() override;

  //! Immutable resident texture/descriptor retained by a rendering consumer.
  //! The binder must outlive leases; keep frame leases until readers retire.
  struct ReadyTexture {
    std::shared_ptr<const graphics::Texture> texture;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  };

  //! Returns only the intended uploaded texture, never an error/placeholder.
  //! While a lease is held, the entry is not evicted. Releasing the last lease
  //! allows queued eviction on the next OnFrameStart().
  [[nodiscard]] OXGN_VRTX_API auto AcquireReadyTexture(
    const content::ResourceKey& key) -> std::shared_ptr<const ReadyTexture>;

  //! Distinguishes a completed load/upload failure from pending residency.
  [[nodiscard]] OXGN_VRTX_API auto HasResourceFailed(
    const content::ResourceKey& key) const noexcept -> bool;

  //! Must be called once per frame before any GetOrAllocate() calls.
  //! Bounded completion and eviction maintenance; starts no new loads.
  OXGN_VRTX_API auto OnFrameStart() -> void;
  //! Submit queued uploads and retry loads once for a frame with active demand.
  OXGN_VRTX_API auto EnsureFrameResources() -> void;

  //! Must be called once per frame after all rendering.
  OXGN_VRTX_API auto OnFrameEnd() -> void;

  //! Log a summary of the largest resident textures.
  /*!
   Estimates per-texture GPU memory use from the runtime texture descriptor
   (format, dimensions, mips, array layers) and logs the top-N entries.
  */
  OXGN_VRTX_API auto DumpEstimatedTextureMemory(std::size_t top_n) const
    -> void;

  OXGN_VRTX_NDAPI auto GetOrAllocate(const content::ResourceKey& resource_key)
    -> ShaderVisibleIndex override;

  [[nodiscard]] auto TryGetMipLevels(
    const content::ResourceKey& key) const noexcept
    -> std::optional<std::uint32_t> override;

  OXGN_VRTX_NDAPI auto IsResourceReady(
    const content::ResourceKey& key) const noexcept -> bool override;

  //! Illuminance, in lux, that a loaded HDR cube delivers on an upward-facing
  //! surface, measured once when it uploads (see `MeasureCubeIlluminance`).
  //! Nothing until the texture is ready, or for a texture that is not a float
  //! cube.
  [[nodiscard]] OXGN_VRTX_API auto TryGetCubeIlluminance(
    const content::ResourceKey& key) const noexcept -> std::optional<float>;

  [[nodiscard]] OXGN_VRTX_API auto GetPendingUploadCount() const noexcept
    -> std::size_t;
  //! Advances whenever an entry publishes a different index, so a host can
  //! tell that sampled texture content changed without a scene edit.
  [[nodiscard]] OXGN_VRTX_API auto GetResidentContentRevision() const noexcept
    -> std::uint64_t override;
  //! Revision of the content behind `descriptor`, assigned when the
  //! descriptor is registered and never repeated by a recycled slot; zero
  //! for a descriptor this binder does not serve.
  [[nodiscard]] OXGN_VRTX_API auto GetContentRevision(
    ShaderVisibleIndex descriptor) const noexcept -> std::uint64_t;
  [[nodiscard]] OXGN_VRTX_API auto GetPendingUploadBytes() const noexcept
    -> std::size_t;
  [[nodiscard]] OXGN_VRTX_API auto GetDeferredRetryCount() const noexcept
    -> std::size_t;
  [[nodiscard]] OXGN_VRTX_API auto GetPendingUploadByteBudget() const noexcept
    -> std::size_t;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::resources
