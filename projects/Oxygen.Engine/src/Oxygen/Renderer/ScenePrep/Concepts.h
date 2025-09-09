//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>

#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>

namespace oxygen::engine::sceneprep {

//! Concept for algorithms that act on items during collection.
/*!
 RenderItemDataExtractor represents callable algorithms invoked per-item during
 the collection stage which perform CPU-side processing and may update the
 provided `RenderItemProto` and `ScenePrepState` as needed.

 Stage: Collection

 @param F The extractor algorithm type
 */
template <typename F>
concept RenderItemDataExtractor = requires(
  F f, ScenePrepContext& ctx, ScenePrepState& state, RenderItemProto& item) {
  { f(ctx, state, item) } -> std::same_as<void>;
};

//! Concept for simple uploader callables used in finalization.
/*!
 FinalizationUploader represents simple callable algorithms that ensure
 resources are ready for GPU usage. These correspond to EnsureFrameResources()
 calls in the actual Renderer implementation.

 Stage: Finalization

 Contract:
 - Takes ScenePrepState& to access uploaders
 - May perform GPU operations (resource preparation)
 - Should be idempotent

 @param U The uploader algorithm type
 */
template <typename U>
concept FinalizationUploader = requires(U u, ScenePrepState& state) {
  { u(state) } -> std::same_as<void>;
};

//! Concept for draw metadata emitter callables used in finalization.
/*!
 DrawMetadataEmitter represents callable algorithms that generate draw metadata
 from render item data. This maps to the DrawMetadataEmitter::EmitDrawMetadata
 pattern used in the actual implementation.

 Stage: Finalization

 Contract:
 - Per-item processing
 - Takes ScenePrepState& and const RenderItemData reference
 - May update internal state for later upload

 @param E The emitter algorithm type
 */
template <typename E>
concept DrawMetadataEmitter
  = requires(E e, ScenePrepState& state, const RenderItemData& item) {
      { e(state, item) } -> std::same_as<void>;
    };

//! Concept for sorting/partitioning callables used in finalization.
/*!
 FinalizationSorter represents callable algorithms that sort and partition
 draw metadata for efficient rendering. This corresponds to the
 BuildSortingAndPartitions() pattern in the actual implementation.

 Stage: Finalization

 Contract:
 - Takes ScenePrepState& to access internal data structures
 - May reorder and partition data

 @param S The sorter algorithm type
 */
template <typename S>
concept FinalizationSorter = requires(S s, ScenePrepState& state) {
  { s(state) } -> std::same_as<void>;
};

} // namespace oxygen::engine::sceneprep
