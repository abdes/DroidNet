//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::data {
class GeometryAsset;
} // namespace oxygen::data

namespace oxygen::engine::sceneprep {

//! Per-frame geometry cache.
/*!
 Provides fast lookup from geometry asset to its GPU resource handles.
 Reset at the start of each frame to clear stale mappings, but populated
 from the persistent GeometryRegistry.
 */
class GeometryResidencyCache {
public:
  OXGN_RNDR_API GeometryResidencyCache() = default;
  OXGN_RNDR_API ~GeometryResidencyCache() = default;

  OXYGEN_MAKE_NON_COPYABLE(GeometryResidencyCache)
  OXYGEN_DEFAULT_MOVABLE(GeometryResidencyCache)

  //! Set the handle for a geometry asset.
  /*!
   Records the GPU resource handles for a geometry asset.
   Used by uploaders to cache the results of residency operations.

   @param geometry Raw pointer to the geometry asset
   @param handle GPU resource handles for vertex/index buffers
   */
  OXGN_RNDR_API auto SetHandle(
    const data::GeometryAsset* geometry, GeometryHandle handle) -> void;

  //! Get the handle for a geometry asset.
  /*!
   @param geometry Raw pointer to the geometry asset
   @return GPU resource handles if cached, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto GetHandle(
    const data::GeometryAsset* geometry) const -> std::optional<GeometryHandle>;

  //! Reset all cached handles for the next frame.
  /*!
   Clears the cache to prepare for a new frame. Should be called
   at the beginning of each ScenePrep operation.
   */
  OXGN_RNDR_API auto Reset() -> void;

  //! Check if the cache is empty.
  [[nodiscard]] OXGN_RNDR_API auto IsEmpty() const -> bool;

  //! Get the number of cached geometry handles.
  [[nodiscard]] OXGN_RNDR_API auto GetCachedGeometryCount() const
    -> std::size_t;

private:
  std::unordered_map<const data::GeometryAsset*, GeometryHandle>
    geometry_handles_;
};

} // namespace oxygen::engine::sceneprep
