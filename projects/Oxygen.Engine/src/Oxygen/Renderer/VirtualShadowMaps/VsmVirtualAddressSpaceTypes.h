//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

using VsmVirtualShadowMapId = std::uint32_t;

enum class VsmReuseRejectionReason : std::uint8_t {
  kNone = 0,
  kUnspecified = 1,
  kClipLevelCountMismatch = 2,
  kPagesPerAxisMismatch = 3,
  kPageWorldSizeMismatch = 4,
  kDepthRangeMismatch = 5,
  kPageOffsetOutOfRange = 6,
  kLocalLightLayoutMismatch = 7,
  kNoMatchingCurrentLayout = 8,
  kMissingRemapKey = 9,
  kDuplicateRemapKey = 10,
};

OXGN_RNDR_NDAPI auto to_string(VsmReuseRejectionReason value) noexcept -> const
  char*;

struct VsmVirtualPageCoord {
  std::uint32_t level { 0 };
  std::uint32_t page_x { 0 };
  std::uint32_t page_y { 0 };

  auto operator==(const VsmVirtualPageCoord&) const -> bool = default;
};

struct VsmVirtualMapLayout {
  VsmVirtualShadowMapId id { 0 };
  std::string remap_key {};
  std::uint32_t level_count { 0 };
  std::uint32_t pages_per_level_x { 0 };
  std::uint32_t pages_per_level_y { 0 };
  std::uint32_t total_page_count { 0 };
  std::uint32_t first_page_table_entry { 0 };

  auto operator==(const VsmVirtualMapLayout&) const -> bool = default;
};

OXGN_RNDR_NDAPI auto PageCountPerLevel(
  const VsmVirtualMapLayout& layout) noexcept -> std::uint32_t;
OXGN_RNDR_NDAPI auto TryGetPageTableEntryIndex(
  const VsmVirtualMapLayout& layout, const VsmVirtualPageCoord& coord) noexcept
  -> std::optional<std::uint32_t>;

struct VsmClipmapLayout {
  VsmVirtualShadowMapId first_id { 0 };
  std::string remap_key {};
  std::uint32_t clip_level_count { 0 };
  std::uint32_t pages_per_axis { 0 };
  std::uint32_t first_page_table_entry { 0 };
  std::vector<glm::ivec2> page_grid_origin {};
  std::vector<float> page_world_size {};
  std::vector<float> near_depth {};
  std::vector<float> far_depth {};

  auto operator==(const VsmClipmapLayout&) const -> bool = default;
};

OXGN_RNDR_NDAPI auto PageCountPerClipLevel(
  const VsmClipmapLayout& layout) noexcept -> std::uint32_t;
OXGN_RNDR_NDAPI auto TotalPageCount(const VsmClipmapLayout& layout) noexcept
  -> std::uint32_t;
OXGN_RNDR_NDAPI auto TryGetPageTableEntryIndex(const VsmClipmapLayout& layout,
  const VsmVirtualPageCoord& coord) noexcept -> std::optional<std::uint32_t>;

struct VsmClipmapReuseConfig {
  std::int32_t max_page_offset_x { 0 };
  std::int32_t max_page_offset_y { 0 };
  float depth_range_epsilon { 0.0F };
  float page_world_size_epsilon { 0.0F };

  auto operator==(const VsmClipmapReuseConfig&) const -> bool = default;
};

struct VsmVirtualAddressSpaceConfig {
  VsmVirtualShadowMapId first_virtual_id { 1 };
  VsmClipmapReuseConfig clipmap_reuse_config {};
  std::string debug_name {};

  auto operator==(const VsmVirtualAddressSpaceConfig&) const -> bool = default;
};

struct VsmSinglePageLightDesc {
  std::string remap_key {};
  std::string debug_name {};

  auto operator==(const VsmSinglePageLightDesc&) const -> bool = default;
};

struct VsmLocalLightDesc {
  std::string remap_key {};
  std::uint32_t level_count { 1 };
  std::uint32_t pages_per_level_x { 1 };
  std::uint32_t pages_per_level_y { 1 };
  std::string debug_name {};

  auto operator==(const VsmLocalLightDesc&) const -> bool = default;
};

struct VsmDirectionalClipmapDesc {
  std::string remap_key {};
  std::uint32_t clip_level_count { 0 };
  std::uint32_t pages_per_axis { 0 };
  std::vector<glm::ivec2> page_grid_origin {};
  std::vector<float> page_world_size {};
  std::vector<float> near_depth {};
  std::vector<float> far_depth {};
  std::string debug_name {};

  auto operator==(const VsmDirectionalClipmapDesc&) const -> bool = default;
};

struct VsmClipmapReuseResult {
  bool reusable { false };
  std::vector<glm::ivec2> page_offsets {};
  VsmReuseRejectionReason rejection_reason { VsmReuseRejectionReason::kNone };

  auto operator==(const VsmClipmapReuseResult&) const -> bool = default;
};

struct VsmVirtualRemapEntry {
  VsmVirtualShadowMapId previous_id { 0 };
  VsmVirtualShadowMapId current_id { 0 };
  glm::ivec2 page_offset { 0, 0 };
  VsmReuseRejectionReason rejection_reason { VsmReuseRejectionReason::kNone };

  auto operator==(const VsmVirtualRemapEntry&) const -> bool = default;
};

struct VsmVirtualRemapTable {
  std::vector<VsmVirtualRemapEntry> entries {};

  auto operator==(const VsmVirtualRemapTable&) const -> bool = default;
};

struct VsmVirtualAddressSpaceFrame {
  std::uint64_t frame_generation { 0 };
  VsmVirtualAddressSpaceConfig config {};
  std::uint32_t total_page_table_entry_count { 0 };
  std::vector<VsmVirtualMapLayout> local_light_layouts {};
  std::vector<VsmClipmapLayout> directional_layouts {};

  auto operator==(const VsmVirtualAddressSpaceFrame&) const -> bool = default;
};

} // namespace oxygen::renderer::vsm
