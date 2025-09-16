//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/AtlasBuffer.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

//! Manages GPU material resources with deduplication and bindless access.
/*!
 MaterialBinder provides a modern, handle-based API for managing material
 constants with automatic deduplication, persistent caching, and efficient
 upload coordination. It follows the same architectural patterns as
 TransformUploader and GeometryUploader for consistency and maintainability.

 ## Key Features:

 - **Pre-registered core materials**: Default and debug materials are
 pre-registered during construction
 - **Deduplication**: Identical materials share GPU resources using content
 hashing
 - **Handle-based API**: Strong-typed MaterialHandle ensures type safety
 - **Frame lifecycle**: Integrates with Renderer frame and epoch tracking
 - **Bindless rendering**: Stable SRV indices for GPU descriptor arrays
 - **Batch uploads**: Efficient upload coordination via UploadCoordinator
 - **Lazy allocation**: GPU resources created only when needed
 - **Upload timing control**: Renderer controls blocking vs. non-blocking
 uploads

 ## Handle Allocation:

 Handles are allocated using frame-aware slot reuse to prevent unbounded
 growth with dynamic materials. Within each frame, slots are reused in call
 order. Cross-frame deduplication uses content-based hashing for efficiency.

 ## Performance Characteristics:

 - O(1) handle allocation and lookup after initial setup
 - Cross-frame cache persistence enables stable handle allocation
 - Per-frame slot reuse prevents unbounded growth with dynamic materials
 - Sparse uploads for dirty materials only via UploadCoordinator batching
 - Content-based deduplication reduces redundant GPU resource creation
 - Fire-and-forget upload coordination eliminates synchronization overhead

 ## Statistics Tracking:

 MaterialBinder maintains comprehensive telemetry for performance analysis:
 - **total_calls_**: Total GetOrAllocate/Update method invocations
 - **cache_hits_**: Number of cache hits (material already exists)
 - **total_allocations_**: Number of logical material allocations
 - **atlas_allocations_**: Number of atlas element allocations (1 per material)
 - **upload_operations_**: Number of upload operations to staging provider

 Cache hit rate = cache_hits_ / total_calls_ (higher is better for perf)
 Atlas efficiency = atlas_allocations_ / total_allocations_ (should be 1:1)

 ## Usage Pattern:

 ```cpp
 // Frame setup (once per frame with frame slot)
 material_binder->OnFrameStart(frame_slot);

 // Register materials (as needed)
 auto handle = material_binder->GetOrAllocate(material);

 // Prepare GPU resources (before rendering)
 material_binder->EnsureFrameResources();

 // Access bindless index (during rendering)
 auto srv_index = material_binder->GetMaterialsSrvIndex();
 ```

 ## Upload Architecture:

 Uses UploadCoordinator's fire-and-forget approach with automatic batching
 and coalescing. Only dirty materials are uploaded per frame, with the
 coordinator handling efficient merging of contiguous updates.

 ## Frame Lifecycle Requirements:

 1. `OnFrameStart(slot)` must be called once per frame before any other
 operations
 2. `EnsureFrameResources()` must be called before accessing SRV indices
 3. SRV indices remain stable within a frame but may change between frames

 @see TransformUploader for architectural reference
 @see GeometryUploader for architectural reference
 @see UploadCoordinator for upload coordination details
 @see engine::sceneprep::MaterialHandle for handle type definition
*/
class MaterialBinder {
public:
  /*!
   @note MaterialBinder lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the MaterialBinder.
  */
  OXGN_RNDR_API MaterialBinder(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    observer_ptr<engine::upload::StagingProvider> provider);

  OXYGEN_MAKE_NON_COPYABLE(MaterialBinder)
  OXYGEN_MAKE_NON_MOVABLE(MaterialBinder)

  OXGN_RNDR_API ~MaterialBinder();

  //! Must be called once per frame before any other operations.
  OXGN_RNDR_API auto OnFrameStart(
    renderer::RendererTag, oxygen::frame::Slot slot) -> void;

  //! Get or allocate a material handle with content-based deduplication.
  /*!
   Returns existing handle if material content matches a previously registered
   material, otherwise allocates a new handle and prepares the material data
   for GPU upload.

   @param material Material asset to register (must be valid)
   @return Material handle for bindless shader access

   ### Performance Characteristics

   - Time Complexity: O(1) average for hash lookup
   - Memory: Allocates slot in materials array if new
   - Optimization: Content-based deduplication reduces GPU memory usage

   ### Usage Examples

   ```cpp
   auto handle = binder.GetOrAllocate(material_asset);
   if (!binder.IsValidHandle(handle)) {
     // Handle error case
   }
   ```

   @warning Returns invalid handle if material is null or invalid
   @see Update, IsValidHandle
   */
  OXGN_RNDR_API auto GetOrAllocate(
    std::shared_ptr<const data::MaterialAsset> material)
    -> engine::sceneprep::MaterialHandle;

  //! Update an existing material handle with new material data.
  /*!
   Updates the material constants for an existing handle. If the material
   data hasn't changed, this is a no-op.

   @param handle Previously allocated material handle
   @param material New material asset data

  ### Performance Characteristics

  - Time Complexity: O(1) for unchanged materials, O(k) for updates
  - Memory: No additional allocation for unchanged materials
  - Optimization: Content comparison avoids unnecessary GPU updates

  @see GetOrAllocate, IsValidHandle
   */
  auto Update(engine::sceneprep::MaterialHandle handle,
    std::shared_ptr<const data::MaterialAsset> material) -> void;

  //! Check if a handle is valid.
  [[nodiscard]] auto IsValidHandle(
    engine::sceneprep::MaterialHandle handle) const -> bool;

  //! Get read-only access to all material constants.
  [[nodiscard]] auto GetMaterialConstants() const noexcept
    -> std::span<const engine::MaterialConstants>;

  //! Get indices of materials that changed this frame.
  [[nodiscard]] auto GetDirtyIndices() const noexcept
    -> std::span<const std::uint32_t>;

  //! Ensures all material GPU resources are prepared for the current frame.
  //! MUST be called after OnFrameStart() and before any GetMaterialsSrvIndex()
  //! calls. Safe to call multiple times per frame - internally optimized.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Returns the bindless descriptor heap index for the materials SRV.
  //! REQUIRES: EnsureFrameResources() must have been called this frame.
  [[nodiscard]] OXGN_RNDR_API auto GetMaterialsSrvIndex() const
    -> ShaderVisibleIndex;

private:
  // Structure for cached material entries. Storing the index into
  // materials_ allows validating the stored material to avoid false
  // positives from quantization or hash collisions.
  struct MaterialCacheEntry {
    engine::sceneprep::MaterialHandle handle;
    std::uint32_t index;
  };

  // Deduplication and state
  std::unordered_map<std::uint64_t, MaterialCacheEntry> material_key_to_handle_;
  std::vector<std::shared_ptr<const data::MaterialAsset>> materials_;
  std::vector<engine::MaterialConstants> material_constants_;
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'

  // Frame lifecycle management
  std::uint32_t frame_write_count_ { 0U };

  // Statistics tracking
  //! Total number of GetOrAllocate/Update calls made
  std::uint64_t total_calls_ { 0U };
  //! Number of cache hits (material already exists with same content)
  std::uint64_t cache_hits_ { 0U };
  //! Number of logical material allocations (new materials created)
  std::uint64_t total_allocations_ { 0U };
  //! Number of atlas element allocations (1 per material)
  std::uint64_t atlas_allocations_ { 0U };
  //! Number of upload operations submitted to staging provider
  std::uint64_t upload_operations_ { 0U };

  // GPU upload dependencies
  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;

  // Atlas-based material storage (Phase 1+)
  std::unique_ptr<AtlasBuffer> materials_atlas_;
  // Atlas element refs stored per material when atlas path is enabled
  std::vector<AtlasBuffer::ElementRef> material_refs_;

  // Current frame slot for atlas element retirement
  oxygen::frame::Slot current_frame_slot_ { oxygen::frame::kInvalidSlot };

  // Frame resource tracking
  bool uploaded_this_frame_ { false };
};

} // namespace oxygen::renderer::resources
