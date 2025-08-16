//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::data {
class GeometryAsset;
} // namespace oxygen::data

namespace oxygen::engine::sceneprep {

//! Persistent geometry registry with residency tracking.
/*!
 Manages geometry resource residency and handle allocation across frames.
 Tracks which geometry assets are resident in GPU memory and provides
 stable handles for consistent bindless access.
 */
class GeometryRegistry {
public:
  OXGN_RNDR_API GeometryRegistry() = default;
  OXGN_RNDR_API ~GeometryRegistry() = default;

  OXYGEN_MAKE_NON_COPYABLE(GeometryRegistry)
  OXYGEN_DEFAULT_MOVABLE(GeometryRegistry)

  //! Ensure geometry is resident and get its handle.
  /*!
   Checks if the geometry is already resident in GPU memory. If not,
   schedules it for upload and returns a handle that will be valid
   once the upload completes.

   @param geometry Raw pointer to the geometry asset
   @return Handle for accessing geometry buffers on GPU
   */
  OXGN_RNDR_API auto EnsureResident(const data::GeometryAsset* geometry)
    -> GeometryHandle;

  //! Check if geometry is currently resident in GPU memory.
  /*!
   @param geometry Raw pointer to the geometry asset
   @return true if geometry buffers are resident and accessible
   */
  [[nodiscard]] OXGN_RNDR_API auto IsResident(
    const data::GeometryAsset* geometry) const -> bool;

  //! Get the handle for a geometry asset.
  /*!
   @param geometry Raw pointer to the geometry asset
   @return Handle if geometry is registered, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto GetHandle(
    const data::GeometryAsset* geometry) const -> std::optional<GeometryHandle>;

  //! Get the geometry asset for a given handle.
  /*!
   @param handle The geometry handle
   @return Raw pointer to the geometry asset, or nullptr if invalid
   */
  [[nodiscard]] OXGN_RNDR_API auto GetGeometry(
    const GeometryHandle& handle) const -> const data::GeometryAsset*;

  //! Get the total number of registered geometry assets.
  [[nodiscard]] OXGN_RNDR_API auto GetRegisteredGeometryCount() const
    -> std::size_t;

private:
  std::unordered_map<const data::GeometryAsset*, GeometryHandle>
    geometry_to_handle_;
  std::unordered_map<std::uint64_t, const data::GeometryAsset*>
    handle_to_geometry_;
  std::uint32_t next_vertex_buffer_handle_ = 1;
  std::uint32_t next_index_buffer_handle_ = 1;

  auto MakeHandleKey(const GeometryHandle& handle) const -> std::uint64_t;
};

} // namespace oxygen::engine::sceneprep
