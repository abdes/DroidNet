//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualClipmapHelpers.h>

namespace oxygen::renderer::vsm {

namespace {

  template <typename Layout> struct LayoutKeyIndex {
    std::unordered_map<std::string, const Layout*> unique {};
    std::unordered_set<std::string> duplicates {};
  };

  template <typename Layout>
  auto BuildIndexByKey(const std::vector<Layout>& layouts)
    -> LayoutKeyIndex<Layout>
  {
    auto index = LayoutKeyIndex<Layout> {};
    for (const auto& layout : layouts) {
      if (layout.remap_key.empty()) {
        continue;
      }
      if (index.duplicates.contains(layout.remap_key)) {
        continue;
      }

      if (!index.unique.emplace(layout.remap_key, &layout).second) {
        index.unique.erase(layout.remap_key);
        index.duplicates.insert(layout.remap_key);
      }
    }
    return index;
  }

  auto MakeRejectedRemapEntry(const VsmVirtualShadowMapId previous_id,
    const VsmVirtualShadowMapId current_id,
    const VsmReuseRejectionReason rejection_reason,
    const glm::ivec2 page_offset = { 0, 0 }) -> VsmVirtualRemapEntry
  {
    return {
      .previous_id = previous_id,
      .current_id = current_id,
      .page_offset = page_offset,
      .rejection_reason = rejection_reason,
    };
  }

  auto AreLocalLightLayoutsCompatible(
    const VsmVirtualMapLayout& previous_layout,
    const VsmVirtualMapLayout& current_layout) -> bool
  {
    return previous_layout.level_count == current_layout.level_count
      && previous_layout.pages_per_level_x == current_layout.pages_per_level_x
      && previous_layout.pages_per_level_y == current_layout.pages_per_level_y
      && previous_layout.total_page_count == current_layout.total_page_count;
  }

  auto ValidateFrameConfig(
    const VsmVirtualAddressSpaceFrame& frame, const char* frame_label) -> bool
  {
    if (frame.config.first_virtual_id == 0) {
      LOG_F(WARNING, "{} frame generation={} has invalid first_virtual_id=0",
        frame_label, frame.frame_generation);
      return false;
    }

    return true;
  }

  auto ValidateLocalLightLayout(
    const VsmVirtualMapLayout& layout, const char* frame_label) -> bool
  {
    if (layout.id == 0 || layout.level_count == 0
      || layout.pages_per_level_x == 0 || layout.pages_per_level_y == 0) {
      LOG_F(WARNING, "{} local-light layout id={} remap_key=`{}` is malformed",
        frame_label, layout.id, layout.remap_key);
      return false;
    }

    const auto expected_total_page_count = layout.level_count
      * layout.pages_per_level_x * layout.pages_per_level_y;
    if (layout.total_page_count != expected_total_page_count) {
      LOG_F(WARNING,
        "{} local-light layout id={} remap_key=`{}` has inconsistent "
        "total_page_count={} expected={}",
        frame_label, layout.id, layout.remap_key, layout.total_page_count,
        expected_total_page_count);
      return false;
    }

    return true;
  }

  auto ValidateClipmapLayout(
    const VsmClipmapLayout& layout, const char* frame_label) -> bool
  {
    if (layout.first_id == 0 || layout.clip_level_count == 0
      || layout.pages_per_axis == 0) {
      LOG_F(WARNING,
        "{} clipmap layout first_id={} remap_key=`{}` is malformed",
        frame_label, layout.first_id, layout.remap_key);
      return false;
    }

    if (layout.page_grid_origin.size() != layout.clip_level_count
      || layout.clip_min_corner_ls.size() != layout.clip_level_count
      || layout.page_world_size.size() != layout.clip_level_count
      || layout.near_depth.size() != layout.clip_level_count
      || layout.far_depth.size() != layout.clip_level_count) {
      LOG_F(WARNING,
        "{} clipmap layout first_id={} remap_key=`{}` has inconsistent "
        "per-level vector sizes",
        frame_label, layout.first_id, layout.remap_key);
      return false;
    }

    return true;
  }

} // namespace

auto BuildVirtualRemapTable(const VsmVirtualAddressSpaceFrame& previous_frame,
  const VsmVirtualAddressSpaceFrame& current_frame) -> VsmVirtualRemapTable
{
  auto table = VsmVirtualRemapTable {};
  static_cast<void>(ValidateFrameConfig(previous_frame, "previous"));
  static_cast<void>(ValidateFrameConfig(current_frame, "current"));
  const auto previous_local_by_key
    = BuildIndexByKey(previous_frame.local_light_layouts);
  const auto current_local_by_key
    = BuildIndexByKey(current_frame.local_light_layouts);
  const auto previous_clipmap_by_key
    = BuildIndexByKey(previous_frame.directional_layouts);
  const auto current_clipmap_by_key
    = BuildIndexByKey(current_frame.directional_layouts);

  DLOG_F(3,
    "previous local={} current local={} previous clipmaps={} current "
    "clipmaps={}",
    previous_frame.local_light_layouts.size(),
    current_frame.local_light_layouts.size(),
    previous_frame.directional_layouts.size(),
    current_frame.directional_layouts.size());

  for (const auto& previous_layout : previous_frame.local_light_layouts) {
    if (!ValidateLocalLightLayout(previous_layout, "previous")) {
      table.entries.push_back(MakeRejectedRemapEntry(
        previous_layout.id, 0, VsmReuseRejectionReason::kUnspecified));
      continue;
    }

    if (previous_layout.remap_key.empty()) {
      LOG_F(WARNING, "local-light layout id={} is missing remap key",
        previous_layout.id);
      table.entries.push_back(MakeRejectedRemapEntry(
        previous_layout.id, 0, VsmReuseRejectionReason::kMissingRemapKey));
      continue;
    }

    if (previous_local_by_key.duplicates.contains(previous_layout.remap_key)) {
      LOG_F(WARNING, "duplicate previous local-light remap key `{}`",
        previous_layout.remap_key);
      table.entries.push_back(MakeRejectedRemapEntry(
        previous_layout.id, 0, VsmReuseRejectionReason::kDuplicateRemapKey));
      continue;
    }

    if (current_local_by_key.duplicates.contains(previous_layout.remap_key)) {
      LOG_F(WARNING, "duplicate current local-light remap key `{}`",
        previous_layout.remap_key);
      table.entries.push_back(MakeRejectedRemapEntry(
        previous_layout.id, 0, VsmReuseRejectionReason::kDuplicateRemapKey));
      continue;
    }

    const auto current_it
      = current_local_by_key.unique.find(previous_layout.remap_key);
    if (current_it == current_local_by_key.unique.end()) {
      table.entries.push_back(MakeRejectedRemapEntry(previous_layout.id, 0,
        VsmReuseRejectionReason::kNoMatchingCurrentLayout));
      continue;
    }

    const auto& current_layout = *current_it->second;
    if (!ValidateLocalLightLayout(current_layout, "current")) {
      table.entries.push_back(MakeRejectedRemapEntry(previous_layout.id,
        current_layout.id, VsmReuseRejectionReason::kUnspecified));
      continue;
    }

    if (!AreLocalLightLayoutsCompatible(previous_layout, current_layout)) {
      table.entries.push_back(MakeRejectedRemapEntry(previous_layout.id,
        current_layout.id, VsmReuseRejectionReason::kLocalLightLayoutMismatch));
      continue;
    }

    table.entries.push_back(MakeRejectedRemapEntry(
      previous_layout.id, current_layout.id, VsmReuseRejectionReason::kNone));
  }

  for (const auto& previous_layout : previous_frame.directional_layouts) {
    if (!ValidateClipmapLayout(previous_layout, "previous")) {
      for (std::uint32_t clip_index = 0;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index, 0,
            VsmReuseRejectionReason::kUnspecified));
      }
      continue;
    }

    if (previous_layout.remap_key.empty()) {
      LOG_F(WARNING, "clipmap layout first_id={} is missing remap key",
        previous_layout.first_id);
      for (std::uint32_t clip_index = 0;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index, 0,
            VsmReuseRejectionReason::kMissingRemapKey));
      }
      continue;
    }

    if (previous_clipmap_by_key.duplicates.contains(
          previous_layout.remap_key)) {
      LOG_F(WARNING, "duplicate previous clipmap remap key `{}`",
        previous_layout.remap_key);
      for (std::uint32_t clip_index = 0;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index, 0,
            VsmReuseRejectionReason::kDuplicateRemapKey));
      }
      continue;
    }

    if (current_clipmap_by_key.duplicates.contains(previous_layout.remap_key)) {
      LOG_F(WARNING, "duplicate current clipmap remap key `{}`",
        previous_layout.remap_key);
      for (std::uint32_t clip_index = 0;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index, 0,
            VsmReuseRejectionReason::kDuplicateRemapKey));
      }
      continue;
    }

    const auto current_it
      = current_clipmap_by_key.unique.find(previous_layout.remap_key);
    if (current_it == current_clipmap_by_key.unique.end()) {
      for (std::uint32_t clip_index = 0;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index, 0,
            VsmReuseRejectionReason::kNoMatchingCurrentLayout));
      }
      continue;
    }

    const auto& current_layout = *current_it->second;
    if (!ValidateClipmapLayout(current_layout, "current")) {
      for (std::uint32_t clip_index = 0;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index,
            current_layout.first_id + clip_index,
            VsmReuseRejectionReason::kUnspecified));
      }
      continue;
    }

    const auto reuse = ComputeClipmapReuse(previous_layout, current_layout,
      current_frame.config.clipmap_reuse_config);
    const auto shared_level_count = (std::min)(previous_layout.clip_level_count,
      current_layout.clip_level_count);
    for (std::uint32_t clip_index = 0; clip_index < shared_level_count;
      ++clip_index) {
      const auto level_offset = clip_index < reuse.page_offsets.size()
        ? reuse.page_offsets[clip_index]
        : glm::ivec2 { 0, 0 };
      table.entries.push_back(
        MakeRejectedRemapEntry(previous_layout.first_id + clip_index,
          current_layout.first_id + clip_index, reuse.rejection_reason,
          level_offset));
    }

    if (previous_layout.clip_level_count > shared_level_count) {
      for (std::uint32_t clip_index = shared_level_count;
        clip_index < previous_layout.clip_level_count; ++clip_index) {
        table.entries.push_back(
          MakeRejectedRemapEntry(previous_layout.first_id + clip_index, 0,
            VsmReuseRejectionReason::kClipLevelCountMismatch));
      }
    }
  }

  return table;
}

} // namespace oxygen::renderer::vsm
