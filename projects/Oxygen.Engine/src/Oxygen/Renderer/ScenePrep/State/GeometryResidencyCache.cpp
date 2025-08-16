//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/State/GeometryResidencyCache.h>

namespace oxygen::engine::sceneprep {

auto GeometryResidencyCache::SetHandle(
  const data::GeometryAsset* geometry, GeometryHandle handle) -> void
{
  if (geometry == nullptr) {
    return;
  }
  geometry_handles_[geometry] = handle;
}

auto GeometryResidencyCache::GetHandle(
  const data::GeometryAsset* geometry) const -> std::optional<GeometryHandle>
{
  if (geometry == nullptr) {
    return std::nullopt;
  }
  if (const auto it = geometry_handles_.find(geometry);
    it != geometry_handles_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto GeometryResidencyCache::Reset() -> void { geometry_handles_.clear(); }

auto GeometryResidencyCache::IsEmpty() const -> bool
{
  return geometry_handles_.empty();
}

auto GeometryResidencyCache::GetCachedGeometryCount() const -> std::size_t
{
  return geometry_handles_.size();
}

} // namespace oxygen::engine::sceneprep
