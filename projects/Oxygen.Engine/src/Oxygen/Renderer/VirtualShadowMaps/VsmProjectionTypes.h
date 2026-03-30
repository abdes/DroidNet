//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

namespace oxygen::renderer::vsm {

enum class VsmProjectionLightType : std::uint32_t {
  kLocal = 0U,
  kDirectional = 1U,
};

struct VsmProjectionData {
  glm::mat4 view_matrix { 1.0F };
  glm::mat4 projection_matrix { 1.0F };
  glm::vec4 view_origin_ws_pad { 0.0F, 0.0F, 0.0F, 0.0F };
  // XY: directional receiver-depth interval in main-view space.
  // ZW: authored shadow bias / normal bias carried into projection.
  glm::vec4 receiver_depth_range_pad { 0.0F, 0.0F, 0.0F, 0.0F };
  // Directional clipmap corner metadata in integer page units. This must not
  // be interpreted as a fractional raster/sample phase offset.
  glm::ivec2 clipmap_corner_offset { 0, 0 };
  std::uint32_t clipmap_level { 0U };
  std::uint32_t light_type { static_cast<std::uint32_t>(
    VsmProjectionLightType::kLocal) };

  auto operator==(const VsmProjectionData&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmProjectionData>);
static_assert(sizeof(VsmProjectionData) == 176U);
static_assert(offsetof(VsmProjectionData, view_matrix) == 0U);
static_assert(offsetof(VsmProjectionData, projection_matrix) == 64U);
static_assert(offsetof(VsmProjectionData, view_origin_ws_pad) == 128U);
static_assert(offsetof(VsmProjectionData, receiver_depth_range_pad) == 144U);
static_assert(offsetof(VsmProjectionData, clipmap_corner_offset) == 160U);
static_assert(offsetof(VsmProjectionData, clipmap_level) == 168U);
static_assert(offsetof(VsmProjectionData, light_type) == 172U);

inline constexpr std::uint32_t kVsmInvalidLightIndex = 0xffffffffU;
inline constexpr std::uint32_t kVsmInvalidCubeFaceIndex = 0xffffffffU;

// Shared CPU/GPU routing record for one projected virtual-shadow map.
struct VsmPageRequestProjection {
  VsmProjectionData projection {};
  VsmVirtualShadowMapId map_id { 0U };
  std::uint32_t first_page_table_entry { 0U };
  std::uint32_t map_pages_x { 0U };
  std::uint32_t map_pages_y { 0U };
  std::uint32_t pages_x { 0U };
  std::uint32_t pages_y { 0U };
  std::uint32_t page_offset_x { 0U };
  std::uint32_t page_offset_y { 0U };
  std::uint32_t level_count { 1U };
  std::uint32_t coarse_level { 0U };
  std::uint32_t light_index { kVsmInvalidLightIndex };
  std::uint32_t cube_face_index { kVsmInvalidCubeFaceIndex };

  auto operator==(const VsmPageRequestProjection&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmPageRequestProjection>);
static_assert(sizeof(VsmPageRequestProjection) == 224U);
static_assert(offsetof(VsmPageRequestProjection, projection) == 0U);
static_assert(offsetof(VsmPageRequestProjection, map_id) == 176U);
static_assert(
  offsetof(VsmPageRequestProjection, first_page_table_entry) == 180U);
static_assert(offsetof(VsmPageRequestProjection, map_pages_x) == 184U);
static_assert(offsetof(VsmPageRequestProjection, map_pages_y) == 188U);
static_assert(offsetof(VsmPageRequestProjection, pages_x) == 192U);
static_assert(offsetof(VsmPageRequestProjection, pages_y) == 196U);
static_assert(offsetof(VsmPageRequestProjection, page_offset_x) == 200U);
static_assert(offsetof(VsmPageRequestProjection, page_offset_y) == 204U);
static_assert(offsetof(VsmPageRequestProjection, level_count) == 208U);
static_assert(offsetof(VsmPageRequestProjection, coarse_level) == 212U);
static_assert(offsetof(VsmPageRequestProjection, light_index) == 216U);
static_assert(offsetof(VsmPageRequestProjection, cube_face_index) == 220U);

} // namespace oxygen::renderer::vsm
