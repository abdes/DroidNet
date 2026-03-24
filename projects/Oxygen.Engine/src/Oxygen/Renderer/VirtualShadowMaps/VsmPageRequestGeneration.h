//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

// CPU-side reference math for Stage 5 page discovery.
//
// The runtime GPU path lives in VsmPageRequestGeneratorPass, but this helper
// keeps the projection and request-merging policy independently testable.

struct VsmVisiblePixelSample {
  glm::vec3 world_position_ws { 0.0F, 0.0F, 0.0F };
  std::vector<std::uint32_t> affecting_local_light_indices {};

  auto operator==(const VsmVisiblePixelSample&) const -> bool = default;
};

struct VsmPageRequestGenerationOptions {
  bool enable_coarse_pages { true };
  bool enable_light_grid_pruning { true };

  auto operator==(const VsmPageRequestGenerationOptions&) const -> bool
    = default;
};

OXGN_RNDR_NDAPI auto IsValid(
  const VsmPageRequestProjection& projection) noexcept -> bool;

OXGN_RNDR_NDAPI auto TryProjectWorldToPage(
  const VsmPageRequestProjection& projection,
  const glm::vec3& world_position_ws) noexcept
  -> std::optional<VsmVirtualPageCoord>;

OXGN_RNDR_NDAPI auto BuildPageRequests(
  std::span<const VsmPageRequestProjection> projections,
  std::span<const VsmVisiblePixelSample> samples,
  const VsmPageRequestGenerationOptions& options = {}) -> VsmPageRequestSet;

} // namespace oxygen::renderer::vsm
