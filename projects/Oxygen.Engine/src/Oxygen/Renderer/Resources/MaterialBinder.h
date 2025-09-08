//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
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

 Handles are allocated sequentially starting from 0. The first two handles are
 reserved for default and debug materials which are pre-registered during
 construction.

 ## Performance Characteristics:

 - O(1) handle allocation and lookup after initial setup
 - Persistent caching eliminates redundant GPU resource creation
 - Batch uploads minimize GPU submission overhead
 - Frame-coherent dirty tracking reduces unnecessary work

 ## Usage Pattern:

 ```cpp
 // Frame setup (once per frame)
 material_binder->OnFrameStart();

 // Register materials (as needed)
 auto handle = material_binder->GetOrAllocate(material);

 // Prepare GPU resources (before rendering)
 material_binder->EnsureFrameResources();

 // Access bindless index (during rendering)
 auto srv_index = material_binder->GetMaterialsSrvIndex();
 ```

 ## Upload Timing Control:

 The Renderer controls upload timing via UploadCoordinator methods:
 - **Non-blocking**: Call `EnsureFrameResources()` - uploads happen
 asynchronously
 - **Blocking**: Call `uploader_->AwaitAll(tickets)` after
 `EnsureFrameResources()`

 ## Frame Lifecycle Requirements:

 1. `OnFrameStart()` must be called once per frame before any other operations
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
  OXGN_RNDR_API MaterialBinder(Graphics& graphics,
    observer_ptr<engine::upload::UploadCoordinator> uploader);

  OXYGEN_MAKE_NON_COPYABLE(MaterialBinder)
  OXYGEN_MAKE_NON_MOVABLE(MaterialBinder)

  OXGN_RNDR_API ~MaterialBinder();

  //! Must be called once per frame before any other operations.
  auto OnFrameStart() -> void;

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

  //! Returns span of pending upload tickets for synchronous waiting.
  //! Used by Renderer to wait for all uploads to complete this frame.
  [[nodiscard]] auto GetPendingUploadTickets() const
    -> std::span<const engine::upload::UploadTicket>
  {
    return pending_upload_tickets_;
  }

private:
  //! Ensure GPU buffer and SRV are allocated and registered.
  auto EnsureBufferAndSrv(std::shared_ptr<graphics::Buffer>& buffer,
    ShaderVisibleIndex& bindless_index, std::uint64_t size_bytes,
    const char* debug_label) -> bool;

  //! Build sparse upload requests for changed materials.
  auto BuildSparseUploadRequests(const std::vector<std::uint32_t>& indices,
    std::span<const engine::MaterialConstants> src,
    const std::shared_ptr<graphics::Buffer>& dst, const char* debug_name) const
    -> std::vector<engine::upload::UploadRequest>;

  //! Internal resource preparation method.
  auto PrepareMaterialConstants() -> void;

  // Deduplication and state
  std::unordered_map<std::uint64_t, engine::sceneprep::MaterialHandle>
    material_key_to_handle_;
  std::vector<std::shared_ptr<const data::MaterialAsset>> materials_;
  std::vector<engine::MaterialConstants> material_constants_;
  std::vector<std::uint32_t> versions_;
  std::uint32_t global_version_ { 0U };
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'
  engine::sceneprep::MaterialHandle next_handle_ { 0U };

  // GPU upload dependencies
  Graphics& gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;

  // GPU resources
  std::shared_ptr<graphics::Buffer> gpu_materials_buffer_;
  ShaderVisibleIndex materials_bindless_index_ { kInvalidShaderVisibleIndex };

  // Upload tracking
  std::vector<engine::upload::UploadTicket> pending_upload_tickets_;

  // Frame resource tracking
  bool frame_resources_ensured_ { false };
};

} // namespace oxygen::renderer::resources
