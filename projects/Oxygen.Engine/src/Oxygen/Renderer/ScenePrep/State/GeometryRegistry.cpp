//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/State/GeometryRegistry.h>

#include <utility>

namespace oxygen::engine::sceneprep {

auto GeometryRegistry::EnsureResident(const data::GeometryAsset* geometry)
  -> GeometryHandle
{
  if (geometry == nullptr) {
    return GeometryHandle {}; // invalid handle
  }

  if (const auto it = geometry_to_handle_.find(geometry);
    it != geometry_to_handle_.end()) {
    return it->second;
  }

  // Allocate new handles (simple monotonically increasing indices).
  GeometryHandle handle { next_vertex_buffer_handle_++,
    next_index_buffer_handle_++ };

  geometry_to_handle_.emplace(geometry, handle);
  handle_to_geometry_.emplace(MakeHandleKey(handle), geometry);
  return handle;
}

auto GeometryRegistry::IsResident(const data::GeometryAsset* geometry) const
  -> bool
{
  return geometry_to_handle_.find(geometry) != geometry_to_handle_.end();
}

auto GeometryRegistry::GetHandle(const data::GeometryAsset* geometry) const
  -> std::optional<GeometryHandle>
{
  if (geometry == nullptr) {
    return std::nullopt;
  }
  if (const auto it = geometry_to_handle_.find(geometry);
    it != geometry_to_handle_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto GeometryRegistry::GetGeometry(const GeometryHandle& handle) const
  -> const data::GeometryAsset*
{
  const auto key = MakeHandleKey(handle);
  if (const auto it = handle_to_geometry_.find(key);
    it != handle_to_geometry_.end()) {
    return it->second;
  }
  return nullptr;
}

auto GeometryRegistry::GetRegisteredGeometryCount() const -> std::size_t
{
  return geometry_to_handle_.size();
}

auto GeometryRegistry::MakeHandleKey(const GeometryHandle& handle) const
  -> std::uint64_t
{
  // Pack both 32-bit indices into a single 64-bit key: [index | vertex]
  return (static_cast<std::uint64_t>(handle.index_buffer) << 32)
    | static_cast<std::uint64_t>(handle.vertex_buffer);
}

} // namespace oxygen::engine::sceneprep
