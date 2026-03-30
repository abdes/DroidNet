//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>

#include <algorithm>
#include <cmath>

#include <glm/vec4.hpp>

namespace oxygen::renderer::vsm {

namespace {

  [[nodiscard]] auto IsDirectionalProjection(
    const VsmPageRequestProjection& projection) noexcept -> bool
  {
    return projection.projection.light_type
      == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional);
  }

  [[nodiscard]] auto ProjectionLevel(
    const VsmPageRequestProjection& projection) noexcept -> std::uint32_t
  {
    return IsDirectionalProjection(projection)
      ? projection.projection.clipmap_level
      : 0U;
  }

} // namespace

auto IsValid(const VsmPageRequestProjection& projection) noexcept -> bool
{
  if (projection.map_id == 0U || projection.map_pages_x == 0U
    || projection.map_pages_y == 0U || projection.pages_x == 0U
    || projection.pages_y == 0U || projection.level_count == 0U) {
    return false;
  }

  return projection.page_offset_x <= projection.map_pages_x
    && projection.page_offset_y <= projection.map_pages_y
    && projection.pages_x <= projection.map_pages_x - projection.page_offset_x
    && projection.pages_y <= projection.map_pages_y - projection.page_offset_y;
}

auto TryProjectWorldToPage(const VsmPageRequestProjection& projection,
  const glm::vec3& world_position_ws) noexcept
  -> std::optional<VsmVirtualPageCoord>
{
  if (!IsValid(projection)) {
    return std::nullopt;
  }

  const auto world = glm::vec4(world_position_ws, 1.0F);
  const auto view = projection.projection.view_matrix * world;
  const auto clip = projection.projection.projection_matrix * view;
  if (std::abs(clip.w) <= 1.0e-6F || clip.w < 0.0F) {
    return std::nullopt;
  }

  const auto ndc = glm::vec3(clip) / clip.w;
  if (ndc.x < -1.0F || ndc.x > 1.0F || ndc.y < -1.0F || ndc.y > 1.0F
    || ndc.z < 0.0F || ndc.z > 1.0F) {
    return std::nullopt;
  }

  const auto uv_x = ndc.x * 0.5F + 0.5F;
  const auto uv_y = 0.5F - ndc.y * 0.5F;
  const auto local_page_x = std::min(
    static_cast<std::uint32_t>(uv_x * static_cast<float>(projection.pages_x)),
    projection.pages_x - 1U);
  const auto local_page_y = std::min(
    static_cast<std::uint32_t>(uv_y * static_cast<float>(projection.pages_y)),
    projection.pages_y - 1U);

  return VsmVirtualPageCoord {
    .level = ProjectionLevel(projection),
    .page_x = projection.page_offset_x + local_page_x,
    .page_y = projection.page_offset_y + local_page_y,
  };
}

auto TryComputeProjectionLocalPage(const VsmPageRequestProjection& projection,
  const VsmVirtualPageCoord& page) noexcept
  -> std::optional<VsmVirtualPageCoord>
{
  if (!IsValid(projection) || page.page_x < projection.page_offset_x
    || page.page_y < projection.page_offset_y) {
    return std::nullopt;
  }

  if (IsDirectionalProjection(projection)) {
    if (page.level != projection.projection.clipmap_level) {
      return std::nullopt;
    }
  } else if (page.level >= projection.level_count) {
    return std::nullopt;
  }

  const auto local_page_x = page.page_x - projection.page_offset_x;
  const auto local_page_y = page.page_y - projection.page_offset_y;
  if (local_page_x >= projection.pages_x
    || local_page_y >= projection.pages_y) {
    return std::nullopt;
  }

  return VsmVirtualPageCoord {
    .level = page.level,
    .page_x = local_page_x,
    .page_y = local_page_y,
  };
}

auto TryComputePageTableIndex(const VsmPageRequestProjection& projection,
  const VsmVirtualPageCoord& page) noexcept -> std::optional<std::uint32_t>
{
  if (!TryComputeProjectionLocalPage(projection, page).has_value()) {
    return std::nullopt;
  }

  const auto pages_per_level = projection.map_pages_x * projection.map_pages_y;
  return projection.first_page_table_entry + page.level * pages_per_level
    + page.page_y * projection.map_pages_x + page.page_x;
}

} // namespace oxygen::renderer::vsm
