//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
// #include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include "Oxygen/Data/GeometryAsset.h"
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <numeric>

namespace oxygen::engine::sceneprep {

//! Ensure geometry uploader resources are ready for this frame.
/*!
 Calls EnsureFrameResources() on the geometry uploader stored in ScenePrepState.

 @param state ScenePrep working state containing geometry_uploader
 */
inline auto GeometryPrepareResourcesFinalizer(ScenePrepState& state) -> void
{
  DLOG_SCOPE_FUNCTION(INFO);

  if (state.geometry_uploader_) {

    // iterate over the unfiltered items and ensure their geometry resources are
    // created Use existing retained_indices if available, otherwise process all
    // collected items
    // clang-format off
    const auto& indices_to_process = state.retained_indices_.empty()
    ? [&]() -> std::vector<std::size_t> {
      std::vector<std::size_t> all_indices(state.collected_items_.size());
      std::ranges::iota(all_indices, 0);
      return all_indices;
    }()
    : state.retained_indices_;
    // clang-format on
    DLOG_F(INFO, "processing {} items", indices_to_process.size());

    for (const auto item_index : indices_to_process) {
      const auto& item = state.collected_items_[item_index];
      const auto lod_index = item.lod_index;
      const auto submesh_index = item.submesh_index;
      const auto& geom = *item.geometry;
      const auto meshes_span = geom.Meshes();

      const auto& lod_mesh_ptr = meshes_span[lod_index];
      DCHECK_NOTNULL_F(lod_mesh_ptr);
      if (!lod_mesh_ptr->IsValid()) {
        DLOG_F(WARNING,
          "-skipped- invalid lod mesh at index {} for geometry '{}'", lod_index,
          lod_mesh_ptr->GetName());
        continue;
      }
      const auto& lod = *lod_mesh_ptr;

      const auto submeshes_span = lod.SubMeshes();
      const auto& submesh = submeshes_span[submesh_index];
      const auto views_span = submesh.MeshViews();
      DCHECK_F(!views_span.empty());

      // Get the actual SRV indices immediately - no deferred resolution
      if (lod_mesh_ptr->IsValid()) {
        const auto mesh_handle
          = state.geometry_uploader_->GetOrAllocate(*lod_mesh_ptr);
        if (mesh_handle == GeometryHandle { kInvalidBindlessIndex }) {
          DLOG_F(WARNING,
            "-skipped- failed to get geometry handle for mesh '{}'",
            lod_mesh_ptr->GetName());
        }
      }
    }

    state.geometry_uploader_->EnsureFrameResources();
  }
}

inline auto GeometryUploadFinalizer(ScenePrepState& state) -> void
{
  DLOG_SCOPE_FUNCTION(INFO);
  if (state.geometry_uploader_) {
    state.geometry_uploader_->EnsureFrameResources();
  }
}

//! Ensure transform manager resources are ready for this frame.
/*!
 Calls EnsureFrameResources() on the transform manager stored in ScenePrepState.
 This corresponds to the call made in Renderer::PreExecute.

 @param state ScenePrep working state containing transform_mgr
 */
inline auto TransformUploadFinalizer(ScenePrepState& state) -> void
{
  if (state.GetTransformManager) {
    state.GetTransformManager->Upload();
  }
}

//! Ensure material binder resources are ready for this frame.
/*!
 Calls EnsureFrameResources() on the material binder stored in ScenePrepState.
 This corresponds to the call made in Renderer::PreExecute.

 @param state ScenePrep working state containing material_binder
 */
inline auto MaterialUploadFinalizer(ScenePrepState& state) -> void
{
  if (state.material_binder_) {
    state.material_binder_->EnsureFrameResources();
  }
}

#if defined(LATER)

//! Process draw metadata for a render item through the DrawMetadataEmitter.
/*!
 Calls EmitDrawMetadata() on the draw metadata emitter stored in ScenePrepState.
 This corresponds to the per-item processing pattern in the new finalization
 pipeline.

 @param state ScenePrep working state containing draw_metadata_emitter
 @param item RenderItemData to process
 */
inline auto DrawMetadataEmit(ScenePrepState& state, const RenderItemData& item)
  -> void
{
  if (state.draw_metadata_emitter) {
    state.draw_metadata_emitter->EmitDrawMetadata(item);
  }
}

//! Sort and partition draw metadata for efficient rendering.
/*!
 Calls SortAndPartition() on the draw metadata emitter stored in ScenePrepState.
 This corresponds to the BuildSortingAndPartitions call in Renderer.

 @param state ScenePrep working state containing draw_metadata_emitter
 */
inline auto DrawMetadataSortAndPartition(ScenePrepState& state) -> void
{
  if (state.draw_metadata_emitter) {
    state.draw_metadata_emitter->SortAndPartition();
  }
}

#  if 0
//! Resolve geometry SRV indices after geometry resources are ensured.
/*!
 Calls ResolveGeometrySrvIndices() on the draw metadata emitter stored in
 ScenePrepState. This resolves the vertex and index buffer SRV indices that
 were deferred during EmitDrawMetadata, ensuring they are valid after
 GeometryUploader::EnsureFrameResources has been called.

 @param state ScenePrep working state containing draw_metadata_emitter
 */
inline auto DrawMetadataResolveGeometrySrvIndices(ScenePrepState& state) -> void
{
  if (state.draw_metadata_emitter) {
    state.draw_metadata_emitter->ResolveGeometrySrvIndices();
  }
}
#  endif

//! Upload draw metadata to GPU for bindless access.
/*!
 Calls EnsureFrameResources() on the draw metadata emitter stored in
 ScenePrepState. This ensures GPU resources are created and data is uploaded for
 bindless access. The EnsureFrameResources() method internally calls
 UploadDrawMetadata() when needed.

 @param state ScenePrep working state containing draw_metadata_emitter
 */
inline auto DrawMetadataUpload(ScenePrepState& state) -> void
{
  if (state.draw_metadata_emitter) {
    state.draw_metadata_emitter->EnsureFrameResources();
  }
}
#endif // defined(LATER)

} // namespace oxygen::engine::sceneprep
