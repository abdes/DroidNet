//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/State/MaterialRegistry.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::sceneprep {

// Forward: extraction::RenderItemData already declared in Types.h

//! Persistent and per-frame state for ScenePrep operations.
/*!
 Manages both temporary data (cleared each frame) and persistent caches
 (reused across frames). Helper classes are stored as direct members
 for cache efficiency and avoid pointer indirection.
 */
struct ScenePrepState {
  // === Collection Phase Data ===
  //! Raw items collected during scene traversal.
  std::vector<RenderItemData> collected_items;

  //! Indices of items that passed filtering.
  std::vector<std::size_t> filtered_indices;

  //! Pass masks aligned with filtered_indices.
  std::vector<PassMask> pass_masks;

  // === Transform Management ===
  //! Persistent transform deduplication and GPU buffer management.
  std::unique_ptr<oxygen::renderer::resources::TransformUploader> transform_mgr;

  // === Material Management ===
  //! Persistent material registry with deduplication.
  MaterialRegistry material_registry;

  // === Geometry Management ===
  //! Modern geometry uploader with deduplication and bindless access.
  std::unique_ptr<oxygen::renderer::resources::GeometryUploader>
    geometry_uploader;

  //! Reset per-frame data while preserving persistent caches.
  OXGN_RNDR_API auto ResetFrameData() -> void;
};

} // namespace oxygen::engine::sceneprep
