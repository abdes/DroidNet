//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageRequestGeneration.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <unordered_map>

#include <Oxygen/Base/Hash.h>

namespace oxygen::renderer::vsm {

namespace {

  struct RequestKey {
    VsmVirtualShadowMapId map_id { 0 };
    VsmVirtualPageCoord page {};

    auto operator==(const RequestKey&) const -> bool = default;
  };

  struct RequestKeyHash {
    auto operator()(const RequestKey& key) const noexcept -> std::size_t
    {
      auto hash = std::size_t { 0 };
      oxygen::HashCombine(hash, key.map_id);
      oxygen::HashCombine(hash, key.page.level);
      oxygen::HashCombine(hash, key.page.page_x);
      oxygen::HashCombine(hash, key.page.page_y);
      return hash;
    }
  };

  auto IsLightVisibleToSample(const VsmVisiblePixelSample& sample,
    const std::uint32_t light_index) noexcept -> bool
  {
    return std::find(sample.affecting_local_light_indices.begin(),
             sample.affecting_local_light_indices.end(), light_index)
      != sample.affecting_local_light_indices.end();
  }

} // namespace

auto BuildPageRequests(std::span<const VsmPageRequestProjection> projections,
  std::span<const VsmVisiblePixelSample> samples,
  const VsmPageRequestGenerationOptions& options) -> VsmPageRequestSet
{
  auto merged
    = std::unordered_map<RequestKey, VsmPageRequestFlags, RequestKeyHash> {};

  for (const auto& sample : samples) {
    for (const auto& projection : projections) {
      if (!IsValid(projection)) {
        continue;
      }
      if (options.enable_light_grid_pruning
        && projection.light_index != kVsmInvalidLightIndex
        && !IsLightVisibleToSample(sample, projection.light_index)) {
        continue;
      }

      const auto fine_page
        = TryProjectWorldToPage(projection, sample.world_position_ws);
      if (!fine_page.has_value()) {
        continue;
      }

      const auto fine_key = RequestKey {
        .map_id = projection.map_id,
        .page = *fine_page,
      };
      merged[fine_key] |= VsmPageRequestFlags::kRequired;

      if (!options.enable_coarse_pages || projection.level_count <= 1U
        || projection.coarse_level == 0U) {
        continue;
      }

      auto coarse_page = *fine_page;
      coarse_page.level
        = std::min(projection.coarse_level, projection.level_count - 1U);
      const auto coarse_key = RequestKey {
        .map_id = projection.map_id,
        .page = coarse_page,
      };
      merged[coarse_key]
        |= (VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kCoarse);
    }
  }

  auto requests = VsmPageRequestSet {};
  requests.reserve(merged.size());
  for (const auto& [key, flags] : merged) {
    requests.push_back(VsmPageRequest {
      .map_id = key.map_id,
      .page = key.page,
      .flags = flags,
    });
  }

  std::sort(requests.begin(), requests.end(),
    [](const VsmPageRequest& lhs, const VsmPageRequest& rhs) {
      return std::tie(
               lhs.map_id, lhs.page.level, lhs.page.page_y, lhs.page.page_x)
        < std::tie(
          rhs.map_id, rhs.page.level, rhs.page.page_y, rhs.page.page_x);
    });

  return requests;
}

} // namespace oxygen::renderer::vsm
