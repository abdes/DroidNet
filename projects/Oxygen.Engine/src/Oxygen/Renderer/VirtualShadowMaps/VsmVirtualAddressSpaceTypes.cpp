//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

namespace oxygen::renderer::vsm {

auto to_string(const VsmReuseRejectionReason value) noexcept -> const char*
{
  switch (value) {
  case VsmReuseRejectionReason::kNone:
    return "None";
  case VsmReuseRejectionReason::kUnspecified:
    return "Unspecified";
  case VsmReuseRejectionReason::kClipLevelCountMismatch:
    return "ClipLevelCountMismatch";
  case VsmReuseRejectionReason::kPagesPerAxisMismatch:
    return "PagesPerAxisMismatch";
  case VsmReuseRejectionReason::kPageWorldSizeMismatch:
    return "PageWorldSizeMismatch";
  case VsmReuseRejectionReason::kDepthRangeMismatch:
    return "DepthRangeMismatch";
  case VsmReuseRejectionReason::kPageOffsetOutOfRange:
    return "PageOffsetOutOfRange";
  case VsmReuseRejectionReason::kLocalLightLayoutMismatch:
    return "LocalLightLayoutMismatch";
  case VsmReuseRejectionReason::kNoMatchingCurrentLayout:
    return "NoMatchingCurrentLayout";
  case VsmReuseRejectionReason::kMissingRemapKey:
    return "MissingRemapKey";
  case VsmReuseRejectionReason::kDuplicateRemapKey:
    return "DuplicateRemapKey";
  case VsmReuseRejectionReason::kSubPageOffsetMismatch:
    return "SubPageOffsetMismatch";
  }

  return "__NotSupported__";
}

auto PageCountPerLevel(const VsmVirtualMapLayout& layout) noexcept
  -> std::uint32_t
{
  return layout.pages_per_level_x * layout.pages_per_level_y;
}

auto TryGetPageTableEntryIndex(const VsmVirtualMapLayout& layout,
  const VsmVirtualPageCoord& coord) noexcept -> std::optional<std::uint32_t>
{
  if (layout.level_count == 0 || coord.level >= layout.level_count
    || coord.page_x >= layout.pages_per_level_x
    || coord.page_y >= layout.pages_per_level_y) {
    return std::nullopt;
  }

  const auto page_count_per_level = PageCountPerLevel(layout);
  const auto linear_index = coord.level * page_count_per_level
    + coord.page_y * layout.pages_per_level_x + coord.page_x;
  return layout.first_page_table_entry + linear_index;
}

auto PageCountPerClipLevel(const VsmClipmapLayout& layout) noexcept
  -> std::uint32_t
{
  return layout.pages_per_axis * layout.pages_per_axis;
}

auto TotalPageCount(const VsmClipmapLayout& layout) noexcept -> std::uint32_t
{
  return layout.clip_level_count * PageCountPerClipLevel(layout);
}

auto TryGetPageTableEntryIndex(const VsmClipmapLayout& layout,
  const VsmVirtualPageCoord& coord) noexcept -> std::optional<std::uint32_t>
{
  if (layout.clip_level_count == 0 || coord.level >= layout.clip_level_count
    || coord.page_x >= layout.pages_per_axis
    || coord.page_y >= layout.pages_per_axis) {
    return std::nullopt;
  }

  const auto page_count_per_level = PageCountPerClipLevel(layout);
  const auto linear_index = coord.level * page_count_per_level
    + coord.page_y * layout.pages_per_axis + coord.page_x;
  return layout.first_page_table_entry + linear_index;
}

} // namespace oxygen::renderer::vsm
