//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::vortex::sceneprep {
struct GeometryRef;
} // namespace oxygen::vortex::sceneprep

namespace oxygen::vortex::upload {
class StagingProvider;
class UploadCoordinator;
struct UploadTicket;
} // namespace oxygen::vortex::upload

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::vortex::resources {

//===----------------------------------------------------------------------===//
// GeometryUploader: Interns mesh identities, creates buffers, registers SRVs,
// schedules uploads
//===----------------------------------------------------------------------===//

//! Manages GPU geometry resources with stable handles and bindless access.
/*!
 GeometryUploader interns geometry identities and manages the GPU buffers and
 bindless SRV indices required for rendering.

 This type uses a PIMPL implementation (see `GeometryUploader::Impl`) to keep
 the public header lightweight and stable: call sites only need the renderer-
 facing API and do not inherit the full include surface of the upload system,
 resource registry, or mesh data types.

 ### Key Features

 - **Stable identity**: requests are keyed by `(AssetKey, lod_index)` provided
   by `vortex::sceneprep::GeometryRef`.
- **Versioned handles**: `vortex::sceneprep::GeometryHandle` carries index and

generation; stale handles are rejected after residency invalidation.
-
**Nexus-backed lifecycle**: geometry slot indices are managed by Nexus

`FrameDrivenIndexReuse` (allocate/release/reclaim + telemetry).
 - **Bindless
safety**: invalid or non-resident geometry yields invalid SRV indices; callers
must render nothing in that case.
 - **Frame-aware work**: `EnsureFrameResources()` is idempotent within a frame.
 - **Upload coordination**: asynchronous uploads are scheduled through
   `vortex::upload::UploadCoordinator`.

 ### Eviction Policy

 - **Asset-level eviction**: when a `GeometryAsset` is evicted, all GPU
   buffers for its meshes (VB/IB and related SRVs) are released and the
   associated handles become invalid until the asset is reloaded.

 ### Usage Pattern

 ```cpp
 // Per-frame setup
 geo_uploader.OnFrameStart(tag, slot);

 // Register or update geometry
 const auto handle = geo_uploader.GetOrAllocate(geo_ref);

 // Prepare uploads/resources (safe to call multiple times per frame)
 geo_uploader.EnsureFrameResources();

 // Query indices for rendering
 const auto indices = geo_uploader.GetShaderVisibleIndices(handle);
 if (indices.vertex_srv_index == kInvalidShaderVisibleIndex) {
   // Render nothing for this draw.
 }
 ```

 @note The renderer owns upload timing decisions. It may choose to wait on
   `GetPendingUploadTickets()` or continue rendering while assets become
   resident.
 @warning Do not issue draws that reference invalid SRV indices.
 @note Eviction is asset-level: when a GeometryAsset is evicted, all GPU
   buffers for its meshes are released and handles become invalid until the
   asset is reloaded.
 @see MaterialBinder for a reference PIMPL binder design
 @see vortex::upload::UploadCoordinator
*/
class GeometryUploader {
public:
  struct MeshShaderVisibleIndices {
    ShaderVisibleIndex vertex_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex index_srv_index { kInvalidShaderVisibleIndex };
  };

  /*!
   @note GeometryUploader lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the GeometryUploader.
  */
  OXGN_VRTX_API GeometryUploader(observer_ptr<Graphics> gfx,
    observer_ptr<vortex::upload::UploadCoordinator> uploader,
    observer_ptr<vortex::upload::StagingProvider> provider,
    observer_ptr<content::IAssetLoader> asset_loader);

  OXYGEN_MAKE_NON_COPYABLE(GeometryUploader)
  OXYGEN_MAKE_NON_MOVABLE(GeometryUploader)

  OXGN_VRTX_API ~GeometryUploader();

  //! Called once per frame to advance uploader frame lifecycle state.
  OXGN_VRTX_API auto OnFrameStart(vortex::RendererTag, oxygen::frame::Slot slot)
    -> void;

  //! Handle interning and management
  OXGN_VRTX_API auto GetOrAllocate(
    const vortex::sceneprep::GeometryRef& geometry)
    -> vortex::sceneprep::GeometryHandle;

  //! GetOrAllocate with explicit criticality hint for intelligent batch policy
  //! selection
  OXGN_VRTX_API auto GetOrAllocate(
    const vortex::sceneprep::GeometryRef& geometry, bool is_critical)
    -> vortex::sceneprep::GeometryHandle;

  OXGN_VRTX_API auto Update(vortex::sceneprep::GeometryHandle handle,
    const vortex::sceneprep::GeometryRef& geometry) -> void;
  OXGN_VRTX_NDAPI auto IsHandleValid(
    vortex::sceneprep::GeometryHandle handle) const -> bool;

  //! Ensures all geometry GPU resources are prepared for the current frame.
  //! MUST be called after OnFrameStart() and before any Get*SrvIndex() calls.
  //! Safe to call multiple times per frame - internally optimized.
  OXGN_VRTX_API auto EnsureFrameResources() -> void;

  //! Returns the bindless descriptor heap index for vertex buffer SRV.
  //! Will automatically call EnsureFrameResources() if it hasn't been called
  //! this frame.
  OXGN_VRTX_NDAPI auto GetShaderVisibleIndices(
    vortex::sceneprep::GeometryHandle handle) -> MeshShaderVisibleIndices;

  //! Returns the number of pending upload operations.
  //! Useful for debugging and monitoring upload queue health.
  OXGN_VRTX_NDAPI auto GetPendingUploadCount() const -> std::size_t;

  //! Returns span of pending upload tickets for synchronous waiting.
  //! Used by Renderer to wait for all uploads to complete this frame.
  OXGN_VRTX_NDAPI auto GetPendingUploadTickets() const
    -> std::span<const vortex::upload::UploadTicket>;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::resources
