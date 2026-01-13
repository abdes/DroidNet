//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine::sceneprep {
struct GeometryRef;
} // namespace oxygen::engine::sceneprep

namespace oxygen::engine::upload {
class StagingProvider;
class UploadCoordinator;
struct UploadTicket;
} // namespace oxygen::engine::upload

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::renderer::resources {

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
   by `engine::sceneprep::GeometryRef`.
 - **Stable handles**: `engine::sceneprep::GeometryHandle` remains stable for
   a given identity for the renderer lifetime.
 - **Bindless safety**: invalid or non-resident geometry yields invalid SRV
   indices; callers must render nothing in that case.
 - **Frame-aware work**: `EnsureFrameResources()` is idempotent within a frame.
 - **Upload coordination**: asynchronous uploads are scheduled through
   `engine::upload::UploadCoordinator`.

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
 @see MaterialBinder for a reference PIMPL binder design
 @see engine::upload::UploadCoordinator
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
  OXGN_RNDR_API GeometryUploader(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    observer_ptr<engine::upload::StagingProvider> provider);

  OXYGEN_MAKE_NON_COPYABLE(GeometryUploader)
  OXYGEN_MAKE_NON_MOVABLE(GeometryUploader)

  OXGN_RNDR_API ~GeometryUploader();

  //! Called once per frame to reset dirty tracking and advance epoch
  OXGN_RNDR_API auto OnFrameStart(
    renderer::RendererTag, oxygen::frame::Slot slot) -> void;

  //! Handle interning and management
  OXGN_RNDR_API auto GetOrAllocate(
    const engine::sceneprep::GeometryRef& geometry)
    -> engine::sceneprep::GeometryHandle;

  //! GetOrAllocate with explicit criticality hint for intelligent batch policy
  //! selection
  OXGN_RNDR_API auto GetOrAllocate(
    const engine::sceneprep::GeometryRef& geometry, bool is_critical)
    -> engine::sceneprep::GeometryHandle;

  OXGN_RNDR_API auto Update(engine::sceneprep::GeometryHandle handle,
    const engine::sceneprep::GeometryRef& geometry) -> void;
  OXGN_RNDR_NDAPI auto IsHandleValid(
    engine::sceneprep::GeometryHandle handle) const -> bool;

  //! Ensures all geometry GPU resources are prepared for the current frame.
  //! MUST be called after OnFrameStart() and before any Get*SrvIndex() calls.
  //! Safe to call multiple times per frame - internally optimized.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Returns the bindless descriptor heap index for vertex buffer SRV.
  //! Will automatically call EnsureFrameResources() if it hasn't been called
  //! this frame.
  OXGN_RNDR_NDAPI auto GetShaderVisibleIndices(
    engine::sceneprep::GeometryHandle handle) -> MeshShaderVisibleIndices;

  //! Returns the number of pending upload operations.
  //! Useful for debugging and monitoring upload queue health.
  OXGN_RNDR_NDAPI auto GetPendingUploadCount() const -> std::size_t;

  //! Returns span of pending upload tickets for synchronous waiting.
  //! Used by Renderer to wait for all uploads to complete this frame.
  OXGN_RNDR_NDAPI auto GetPendingUploadTickets() const
    -> std::span<const engine::upload::UploadTicket>;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::renderer::resources
