//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>
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

//! Owns geometry residency keyed by asset and LOD, with Nexus-versioned
//! handles.
/*!
 Upload results remain pending until the owner publishes their descriptors.
 Eviction detaches the live asset identity before bounded reclamation; old
 handles cannot read or publish into a replacement. Graphics retains resources
 referenced by recorded GPU work. Call OnFrameStart before frame resource use.
*/
class GeometryUploader {
public:
  struct MaintenanceLimits {
    std::size_t max_pending_upload_visits_per_frame { 128U };
    std::size_t max_reclaimed_lods_per_frame { 64U };
  };

  struct MeshShaderVisibleIndices {
    ShaderVisibleIndex vertex_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex index_srv_index { kInvalidShaderVisibleIndex };
    //! Resident content version, independent of handle/descriptor lifetime.
    //! Zero means complete resident content is not available.
    std::uint64_t content_revision { 0U };
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

  OXGN_VRTX_API GeometryUploader(observer_ptr<Graphics> gfx,
    observer_ptr<vortex::upload::UploadCoordinator> uploader,
    observer_ptr<vortex::upload::StagingProvider> provider,
    observer_ptr<content::IAssetLoader> asset_loader, MaintenanceLimits limits);

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

  //! Advances whenever a geometry upload completes and its buffers become
  //! resident, so a host can tell drawable content changed without a scene
  //! edit.
  OXGN_VRTX_NDAPI auto GetResidentContentRevision() const noexcept
    -> std::uint64_t;

  //! Returns the number of pending upload operations.
  //! Useful for debugging and monitoring upload queue health.
  OXGN_VRTX_NDAPI auto GetPendingUploadCount() const -> std::size_t;

  //! Builds an owned snapshot of current pending uploads on demand.
  OXGN_VRTX_NDAPI auto GetPendingUploadTickets() const
    -> std::vector<vortex::upload::UploadTicket>;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::resources
