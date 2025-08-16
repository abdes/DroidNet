//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::sceneprep {

//! Per-frame cache mapping item indices to transform handles.
/*!
 Provides fast lookup from RenderItemData index to the corresponding
 TransformHandle allocated by TransformManager. Reset at the start
 of each frame to clear stale mappings.
 */
class TransformBatchCache {
public:
  OXGN_RNDR_API TransformBatchCache() = default;
  OXGN_RNDR_API ~TransformBatchCache() = default;

  OXYGEN_MAKE_NON_COPYABLE(TransformBatchCache)
  OXYGEN_DEFAULT_MOVABLE(TransformBatchCache)

  //! Map an item index to its allocated transform handle.
  /*!
   Records the association between a RenderItemData index and the
   TransformHandle allocated for its world transform.

   @param item_idx Index of the item in the collected items array
   @param handle Transform handle allocated by TransformManager
   */
  OXGN_RNDR_API auto MapItemToHandle(
    std::size_t item_idx, TransformHandle handle) -> void;

  //! Get the transform handle for a given item index.
  /*!
   @param item_idx Index of the item in the collected items array
   @return Transform handle if mapped, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto GetHandle(std::size_t item_idx) const
    -> std::optional<TransformHandle>;

  //! Reset all mappings for the next frame.
  /*!
   Clears the cache to prepare for a new frame. Should be called
   at the beginning of each ScenePrep operation.
   */
  OXGN_RNDR_API auto Reset() -> void;

  //! Check if the cache is empty.
  [[nodiscard]] OXGN_RNDR_API auto IsEmpty() const -> bool;

  //! Get the number of mapped items.
  [[nodiscard]] OXGN_RNDR_API auto GetMappedItemCount() const -> std::size_t;

private:
  std::vector<TransformHandle> item_to_handle_;
  std::size_t mapped_count_ = 0;
};

} // namespace oxygen::engine::sceneprep
