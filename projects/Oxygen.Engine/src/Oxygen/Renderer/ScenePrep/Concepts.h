//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <span>

#include <Oxygen/Renderer/ScenePrep/Types.h>

namespace oxygen::engine::extraction {
struct RenderItemData;
} // namespace oxygen::engine::extraction

namespace oxygen::engine {
struct RenderItem;
struct DrawMetadata;
} // namespace oxygen::engine

namespace oxygen::data {
class MeshView;
} // namespace oxygen::data

namespace oxygen::engine::sceneprep {

//! Concept for algorithms that filter items and compute pass masks.
/*!
 Filter algorithms decide per-item participation in render passes.
 They are CPU-only and must not trigger GPU operations.

 Stage: Finalization

 Contract:
 - Pure function - no side effects beyond updating ScenePrepState
 - Deterministic - identical inputs must yield identical results
 - Pass mask uses bitset where each bit = renderer-defined pass ID

 @param F The filter algorithm type
 */
template <typename F>
concept ScenePrepFilter = requires(F f, const ScenePrepContext& ctx,
  ScenePrepState& state, const extraction::RenderItemData& data) {
  { f(ctx, state, data) } -> std::same_as<PassMask>;
};

//! Concept for algorithms that perform batch GPU uploads.
/*!
 Uploader algorithms process filtered items in batches to upload
 resources to GPU and update state caches. Only Uploaders may
 perform GPU operations.

 Stage: Finalization

 Contract:
 - Batch processing only - operates on spans of item indices
 - GPU uploads allowed - may trigger resource creation/updates
 - Must update corresponding state caches for Assemblers

 @param U The uploader algorithm type
 */
template <typename U>
concept ScenePrepUploader = requires(U u, const ScenePrepContext& ctx,
  ScenePrepState& state, std::span<const extraction::RenderItemData> all_items,
  std::span<const std::size_t> indices) {
  { u(ctx, state, all_items, indices) } -> std::same_as<void>;
};

//! Concept for algorithms that update state at batch level.
/*!
 BatchUpdater algorithms perform CPU-side computations over
 batches of items, typically for sorting, partitioning, or
 building lookup tables.

 Contract:
 - CPU-only - no GPU operations allowed
 - Batch processing - operates on spans of item indices
 - May update ScenePrepState with computed results

 Stage: Finalization

 @param B The batch updater algorithm type
 */
template <typename B>
concept ScenePrepBatchUpdater = requires(B b, const ScenePrepContext& ctx,
  ScenePrepState& state, std::span<const extraction::RenderItemData> all_items,
  std::span<const std::size_t> indices) {
  { b(ctx, state, all_items, indices) } -> std::same_as<void>;
};

//! Concept for algorithms that update individual items during assembly.
/*!
 ItemUpdater algorithms perform per-item CPU-side updates during
 the assembly phase, typically for computing flags or properties
 that depend on finalized state.

 Stage: Finalization

 Contract:
 - CPU-only - no GPU operations allowed
 - Per-item processing - called once per render item
 - May read from ScenePrepState and update output RenderItem

 @param I The item updater algorithm type
 */
template <typename I>
concept ScenePrepItemUpdater
  = requires(I i, const ScenePrepContext& ctx, ScenePrepState& state,
    const extraction::RenderItemData& data, RenderItem& output) {
      { i(ctx, state, data, output) } -> std::same_as<void>;
    };

//! Concept for algorithms that assemble final RenderItem data.
/*!
 Assembler algorithms build the final GPU-ready RenderItem from
 input data and cached state. They read from state caches populated
 by Uploaders and must not perform GPU operations.

 Stage: Finalization

 Contract:
 - CPU-only - no GPU operations allowed
 - Per-item processing - called once per render item
 - Reads from state caches (transform, material, geometry)
 - Builds final RenderItem with GPU resource handles/indices

 @param A The assembler algorithm type
 */
template <typename A>
concept ScenePrepAssembler
  = requires(A a, const ScenePrepContext& ctx, ScenePrepState& state,
    const extraction::RenderItemData& data, RenderItem& output) {
      { a(ctx, state, data, output) } -> std::same_as<void>;
    };

//! Concept for algorithms that create draw metadata.
/*!
 DrawMetadataMaker algorithms generate per-draw metadata (index ranges,
 vertex counts, etc.) for mesh views. Used during the assembly phase
 to create GPU-compatible draw commands.

 Stage: Finalization

 Contract:
 - CPU-only - no GPU operations allowed
 - Per-mesh-view processing - called for each drawable mesh view
 - Returns DrawMetadata with ranges for GPU draw commands
 - Must provide data compatible with bindless rendering

 @param M The draw metadata maker algorithm type
 */
template <typename M>
concept ScenePrepDrawMetadataMaker = requires(M m, const ScenePrepContext& ctx,
  const ScenePrepState& state, const data::MeshView& view) {
  { m(ctx, state, view) } -> std::same_as<DrawMetadata>;
};

} // namespace oxygen::engine::sceneprep
