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

//! Per-frame material upload cache.
/*!
 Provides fast lookup from RenderItemData index to the corresponding
 MaterialHandle. Reset at the start of each frame to clear stale data.
 */
class MaterialUploadCache {
public:
  OXGN_RNDR_API MaterialUploadCache() = default;
  OXGN_RNDR_API ~MaterialUploadCache() = default;

  OXYGEN_MAKE_NON_COPYABLE(MaterialUploadCache)
  OXYGEN_DEFAULT_MOVABLE(MaterialUploadCache)

  //! Record material handle for an item index.
  /*!
   Associates a RenderItemData index with the MaterialHandle
   from the material registry or upload process.

   @param item_idx Index of the item in the collected items array
   @param handle Material handle from MaterialRegistry
   */
  OXGN_RNDR_API auto RecordMaterialIndex(
    std::size_t item_idx, MaterialHandle handle) -> void;

  //! Get the material handle for a given item index.
  /*!
   @param item_idx Index of the item in the collected items array
   @return Material handle if recorded, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto GetMaterialHandle(std::size_t item_idx) const
    -> std::optional<MaterialHandle>;

  //! Reset all cached data for the next frame.
  /*!
   Clears the cache to prepare for a new frame. Should be called
   at the beginning of each ScenePrep operation.
   */
  OXGN_RNDR_API auto Reset() -> void;

  //! Check if the cache is empty.
  [[nodiscard]] OXGN_RNDR_API auto IsEmpty() const -> bool;

  //! Get the number of cached material mappings.
  [[nodiscard]] OXGN_RNDR_API auto GetCachedMaterialCount() const
    -> std::size_t;

private:
  std::vector<MaterialHandle> item_to_material_;
  std::size_t cached_count_ = 0;
};

} // namespace oxygen::engine::sceneprep
