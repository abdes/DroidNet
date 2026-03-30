//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <glm/vec3.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

// Shared projection-routing contract used by page discovery and raster-job
// preparation.
//
// A VsmPageRequestProjection may cover either:
// - an entire virtual map
// - a routed subregion inside one owning map, such as a point-light cube face
//   packed into a shared local-light layout

OXGN_RNDR_NDAPI auto IsValid(
  const VsmPageRequestProjection& projection) noexcept -> bool;

OXGN_RNDR_NDAPI auto TryProjectWorldToPage(
  const VsmPageRequestProjection& projection,
  const glm::vec3& world_position_ws) noexcept
  -> std::optional<VsmVirtualPageCoord>;

OXGN_RNDR_NDAPI auto TryComputeProjectionLocalPage(
  const VsmPageRequestProjection& projection,
  const VsmVirtualPageCoord& page) noexcept
  -> std::optional<VsmVirtualPageCoord>;

OXGN_RNDR_NDAPI auto TryComputePageTableIndex(
  const VsmPageRequestProjection& projection,
  const VsmVirtualPageCoord& page) noexcept -> std::optional<std::uint32_t>;

} // namespace oxygen::renderer::vsm
