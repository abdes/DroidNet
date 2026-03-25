// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.h>

#include <algorithm>
#include <optional>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>

namespace oxygen::renderer::vsm {

namespace {

  auto IsRasterPreparationAction(const VsmAllocationAction action) noexcept
    -> bool
  {
    switch (action) {
    case VsmAllocationAction::kAllocateNew:
    case VsmAllocationAction::kInitializeOnly:
      return true;
    case VsmAllocationAction::kReuseExisting:
    case VsmAllocationAction::kEvict:
    case VsmAllocationAction::kReject:
      return false;
    }

    return false;
  }

  auto HasStaticOnlyFlag(const VsmPageRequestFlags flags) noexcept -> bool
  {
    return (flags & VsmPageRequestFlags::kStaticOnly)
      != VsmPageRequestFlags::kNone;
  }

  auto BuildProjectionLookup(
    std::span<const VsmPageRequestProjection> projections)
    -> std::unordered_map<VsmVirtualShadowMapId,
      std::vector<const VsmPageRequestProjection*>>
  {
    auto lookup = std::unordered_map<VsmVirtualShadowMapId,
      std::vector<const VsmPageRequestProjection*>> {};

    for (const auto& projection : projections) {
      if (!IsValid(projection)) {
        continue;
      }

      lookup[projection.map_id].push_back(&projection);
    }

    return lookup;
  }

} // namespace

auto BuildShadowRasterPageJobs(const VsmPageAllocationFrame& frame,
  const VsmPhysicalPoolSnapshot& physical_pool,
  const std::span<const VsmPageRequestProjection> projections)
  -> std::vector<VsmShadowRasterPageJob>
{
  auto jobs = std::vector<VsmShadowRasterPageJob> {};
  if (!frame.is_ready) {
    LOG_F(WARNING,
      "BuildShadowRasterPageJobs: skipping because the allocation frame is "
      "not ready");
    return jobs;
  }

  if (!physical_pool.is_available || physical_pool.page_size_texels == 0U
    || physical_pool.tile_capacity == 0U || physical_pool.tiles_per_axis == 0U
    || physical_pool.slice_count == 0U) {
    LOG_F(WARNING,
      "BuildShadowRasterPageJobs: skipping because the physical pool snapshot "
      "is incomplete");
    return jobs;
  }

  const auto projection_lookup = BuildProjectionLookup(projections);
  jobs.reserve(frame.plan.decisions.size());

  for (const auto& decision : frame.plan.decisions) {
    if (!IsRasterPreparationAction(decision.action)) {
      continue;
    }

    const auto projection_it = projection_lookup.find(decision.request.map_id);
    if (projection_it == projection_lookup.end()) {
      LOG_F(WARNING,
        "BuildShadowRasterPageJobs: missing projection route for map_id={} "
        "(action={})",
        decision.request.map_id, to_string(decision.action));
      continue;
    }

    const auto& routes = projection_it->second;
    const VsmPageRequestProjection* matched_projection = nullptr;
    auto matched_projection_page = VsmVirtualPageCoord {};
    auto page_table_index = std::optional<std::uint32_t> {};
    auto ambiguous_route = false;
    for (const auto* route : routes) {
      const auto projection_page
        = TryComputeProjectionLocalPage(*route, decision.request.page);
      if (!projection_page.has_value()) {
        continue;
      }

      const auto candidate_page_table_index
        = TryComputePageTableIndex(*route, decision.request.page);
      CHECK_F(candidate_page_table_index.has_value(),
        "BuildShadowRasterPageJobs: a matched projection route must resolve a "
        "page-table index");

      if (matched_projection != nullptr) {
        ambiguous_route = true;
        break;
      }

      matched_projection = route;
      matched_projection_page = *projection_page;
      page_table_index = candidate_page_table_index;
    }

    if (ambiguous_route) {
      LOG_F(WARNING,
        "BuildShadowRasterPageJobs: overlapping projection routes for "
        "map_id={} level={} page=({}, {})",
        decision.request.map_id, decision.request.page.level,
        decision.request.page.page_x, decision.request.page.page_y);
      continue;
    }

    if (matched_projection == nullptr || !page_table_index.has_value()) {
      LOG_F(WARNING,
        "BuildShadowRasterPageJobs: missing projection route for map_id={} "
        "level={} page=({}, {})",
        decision.request.map_id, decision.request.page.level,
        decision.request.page.page_x, decision.request.page.page_y);
      continue;
    }

    const auto physical_coord = TryConvertToCoord(
      decision.current_physical_page, physical_pool.tile_capacity,
      physical_pool.tiles_per_axis, physical_pool.slice_count);
    if (!physical_coord.has_value()) {
      LOG_F(WARNING,
        "BuildShadowRasterPageJobs: invalid physical page index={} for pool "
        "capacity={} tiles_per_axis={} slices={}",
        decision.current_physical_page.value, physical_pool.tile_capacity,
        physical_pool.tiles_per_axis, physical_pool.slice_count);
      continue;
    }

    const auto left = physical_coord->tile_x * physical_pool.page_size_texels;
    const auto top = physical_coord->tile_y * physical_pool.page_size_texels;
    const auto right = left + physical_pool.page_size_texels;
    const auto bottom = top + physical_pool.page_size_texels;

    jobs.push_back(VsmShadowRasterPageJob {
      .page_table_index = *page_table_index,
      .map_id = decision.request.map_id,
      .virtual_page = decision.request.page,
      .projection_page = matched_projection_page,
      .physical_page = decision.current_physical_page,
      .physical_coord = *physical_coord,
      .projection = *matched_projection,
      .viewport = oxygen::ViewPort {
        .top_left_x = static_cast<float>(left),
        .top_left_y = static_cast<float>(top),
        .width = static_cast<float>(physical_pool.page_size_texels),
        .height = static_cast<float>(physical_pool.page_size_texels),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      },
      .scissors = oxygen::Scissors {
        .left = static_cast<std::int32_t>(left),
        .top = static_cast<std::int32_t>(top),
        .right = static_cast<std::int32_t>(right),
        .bottom = static_cast<std::int32_t>(bottom),
      },
      .static_only = HasStaticOnlyFlag(decision.request.flags),
    });
  }

  std::sort(jobs.begin(), jobs.end(),
    [](const VsmShadowRasterPageJob& lhs, const VsmShadowRasterPageJob& rhs) {
      if (lhs.page_table_index != rhs.page_table_index) {
        return lhs.page_table_index < rhs.page_table_index;
      }
      return lhs.physical_page.value < rhs.physical_page.value;
    });

  return jobs;
}

} // namespace oxygen::renderer::vsm
