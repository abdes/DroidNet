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

 ### Contracts

 - Per-item processing, with no mutation of RenderItemData
 - May, and most likely will, mutate the ScenePrepState
*/
template <typename F>
concept RenderItemDataExtractor = requires(
  F f, ScenePrepContext& ctx, ScenePrepState& state, RenderItemProto& item) {
  { f(ctx, state, item) } -> std::same_as<void>;
};

//! Concept for finalizer callables used in finalization, responsible for
//! preparing GPU resources and associated stable handles.
/*!
 ### Contracts

 - Bulk processing of collected/filtered items
 - May, but most likely will not, mutate the ScenePrepState
 - Must ensure stable handles are allocated, and become available to subsequent
   stages, for all processed items

 @note: Typically use the `GetOrAllocate` API of the respective scene prep
 workers.
*/
template <typename U>
concept Finalizer = requires(U u, ScenePrepState& state) {
  { u(state) } -> std::same_as<void>;
};

//! Concept for uploader callables used in finalization, responsible for
//! uploading CPU prepared data to the GPU resources created by finalizers.
/*!
 ### Contracts

 - May not mutate the ScenePrepState

 @note: Typically use the `EnsureFrameResources` API of the respective scene
 prep workers. The implementation should be idempotentnt, and resilient against
 the *optional* prior calls to `GetOrAllocate` of the respective workers.
*/
template <typename U>
concept Uploader = requires(U u, const ScenePrepState& state) {
  { u(state) } -> std::same_as<void>;
};

//! Concept for draw metadata emitter callables used in finalization.
/*!
 DrawMetadataEmitter represents callable algorithms that generate draw metadata
 from render items data. This maps to the DrawMetadataEmitter::EmitDrawMetadata
 pattern used in the actual implementation.

 ### Contracts

 - Per-item processing, with no mutation of RenderItemData
 - May, and most likely will, mutate the ScenePrepState
 - Contributes the CPU data for later upload of draw metadata
*/
template <typename E>
concept DrawMetadataEmitter
  = requires(E e, ScenePrepState& state, const RenderItemData& item) {
      { e(state, item) } -> std::same_as<void>;
    };

} // namespace oxygen::engine::sceneprep
