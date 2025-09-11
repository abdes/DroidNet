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

inline auto GeometryUploadFinalizer(const ScenePrepState& state) -> void
{
  if (state.GetGeometryUploader()) {
    state.GetGeometryUploader()->EnsureFrameResources();
  }
}
//! Ensure transform manager resources are ready for this frame.
/*!
 Calls EnsureFrameResources() on the transform manager stored in ScenePrepState.
 This corresponds to the call made in Renderer::PreExecute.

 @param state ScenePrep working state containing transform_mgr
 */
inline auto TransformUploadFinalizer(const ScenePrepState& state) -> void
{
  if (state.GetTransformUploader()) {
    state.GetTransformUploader()->EnsureFrameResources();
  }
}

#if defined(LATER)

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
#endif

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
