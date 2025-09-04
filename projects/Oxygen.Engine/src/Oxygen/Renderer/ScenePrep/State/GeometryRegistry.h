//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
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
 Tracks which geometry assets have been assigned GPU buffer indices and
 provides stable handles for consistent bindless access. The current
 implementation performs a simple monotonically increasing allocation for
 vertex and index buffer indices; future work may integrate a proper
 residency manager / free-list.
 */
class GeometryRegistry {
public:
  OXGN_RNDR_API GeometryRegistry() = default;
  OXGN_RNDR_API ~GeometryRegistry() = default;

  OXYGEN_MAKE_NON_COPYABLE(GeometryRegistry)
  OXYGEN_DEFAULT_MOVABLE(GeometryRegistry)

  //! Deprecated: use GetOrRegisterGeometry (transitional alias to reduce
  //! churn).
  OXGN_RNDR_API auto EnsureResident(const data::GeometryAsset* geometry)
    -> GeometryHandle
  {
    return GetOrRegisterGeometry(geometry);
  }

  //! Get (or register) geometry and return its stable handle.
  /*!
   Idempotent: returns existing handle on repeated calls with the same asset
   pointer. Accepts nullptr and returns the sentinel handle (all zero fields)
   in that case.

   @param geometry Raw pointer to the geometry asset (may be nullptr)
   @return Stable geometry handle ({0,0} sentinel when nullptr)

  ### Performance Characteristics

  - Time Complexity: Expected O(1) average (unordered_map lookup)
  - Memory: Forward + reverse map entry on first registration
  - Optimization: Null and already-registered fast paths

  @see GetHandle, LookupGeometryHandle, GetGeometry, IsValidHandle,
  IsSentinelHandle
   */
  OXGN_RNDR_API auto GetOrRegisterGeometry(const data::GeometryAsset* geometry)
    -> GeometryHandle;

  //! Result type returned by mesh provision callback.
  struct GeometryProvisionResult {
    uint32_t vb; // vertex buffer bindless index
    uint32_t ib; // index buffer bindless index
  };

  //! Register a Mesh pointer (vertex/index buffer pair) and obtain stable
  //! handle.
  /*! Idempotent by raw Mesh* address. The caller supplies a provision function
   that ensures GPU resources exist (creating/uploading if needed) and returns
   the final bindless indices.

   @param mesh Raw Mesh pointer (nullptr -> sentinel)
   @param provision_fn Functor: () -> GeometryProvisionResult
   @return Stable GeometryHandle (sentinel {0,0} if mesh null)

  ### Performance
  - O(1) average lookup; single unordered_map for mesh entries
  - provision_fn invoked only on first registration
  */
  template <typename ProvisionFn>
  auto GetOrRegisterMesh(const void* mesh, ProvisionFn&& provision_fn)
    -> GeometryHandle
  {
    if (mesh == nullptr) {
      return GeometryHandle {}; // sentinel
    }
    if (const auto it = mesh_to_handle_.find(mesh);
      it != mesh_to_handle_.end()) {
      return it->second;
    }
    const GeometryProvisionResult res = provision_fn();
    GeometryHandle handle { res.vb, res.ib };
    mesh_to_handle_.emplace(mesh, handle);
    handle_to_mesh_.emplace(MakeHandleKey(handle), mesh);
    return handle;
  }

  //! Lookup mesh handle without side effects.
  [[nodiscard]] auto LookupMeshHandle(const void* mesh) const
    -> std::optional<GeometryHandle>
  {
    if (!mesh)
      return std::nullopt;
    if (const auto it = mesh_to_handle_.find(mesh); it != mesh_to_handle_.end())
      return it->second;
    return std::nullopt;
  }

  //! Check if geometry is currently registered.
  [[nodiscard]] OXGN_RNDR_API auto IsResident(
    const data::GeometryAsset* geometry) const -> bool;

  //! Get the handle for a geometry asset (no registration side-effects).
  [[nodiscard]] OXGN_RNDR_API auto GetHandle(
    const data::GeometryAsset* geometry) const -> std::optional<GeometryHandle>;

  //! Lookup a geometry handle without registration side-effects (synonym).
  [[nodiscard]] OXGN_RNDR_API auto LookupGeometryHandle(
    const data::GeometryAsset* geometry) const -> std::optional<GeometryHandle>
  {
    return GetHandle(geometry);
  }

  //! Get the geometry asset for a given handle.
  /*! @return Raw pointer to the geometry asset, or nullptr if invalid */
  [[nodiscard]] OXGN_RNDR_API auto GetGeometry(
    const GeometryHandle& handle) const -> const data::GeometryAsset*;

  //! Get the total number of registered geometry assets.
  [[nodiscard]] OXGN_RNDR_API auto GetRegisteredGeometryCount() const
    -> std::size_t;

  //! Check if a handle is valid (non-sentinel and registered).
  [[nodiscard]] OXGN_RNDR_API auto IsValidHandle(
    const GeometryHandle& handle) const -> bool;

  //! Check if a handle is the sentinel (null) handle.
  [[nodiscard]] static constexpr auto IsSentinelHandle(
    const GeometryHandle& handle) noexcept -> bool
  {
    return handle.vertex_buffer == 0U && handle.index_buffer == 0U;
  }

private:
  std::unordered_map<const data::GeometryAsset*, GeometryHandle>
    geometry_to_handle_;
  std::unordered_map<std::uint64_t, const data::GeometryAsset*>
    handle_to_geometry_;
  // Mesh-level registration (raw pointer key)
  std::unordered_map<const void*, GeometryHandle> mesh_to_handle_;
  std::unordered_map<std::uint64_t, const void*> handle_to_mesh_;
  std::uint32_t next_vertex_buffer_handle_ = 1;
  std::uint32_t next_index_buffer_handle_ = 1;

  auto MakeHandleKey(const GeometryHandle& handle) const -> std::uint64_t;
};

} // namespace oxygen::engine::sceneprep
