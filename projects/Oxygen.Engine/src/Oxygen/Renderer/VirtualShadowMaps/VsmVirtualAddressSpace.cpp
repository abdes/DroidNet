//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualClipmapHelpers.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

namespace {

auto ValidateFrameConfig(
  const oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig& config) -> void
{
  if (config.first_virtual_id == 0) {
    throw std::invalid_argument(
      "VsmVirtualAddressSpace: first_virtual_id must be non-zero");
  }
}

auto ValidatePagedLocalLightDesc(
  const oxygen::renderer::vsm::VsmLocalLightDesc& desc) -> void
{
  if (desc.level_count == 0 || desc.pages_per_level_x == 0
    || desc.pages_per_level_y == 0) {
    throw std::invalid_argument(
      "VsmVirtualAddressSpace: paged local-light layouts require non-zero "
      "level and page dimensions");
  }
}

auto ValidateDirectionalClipmapDesc(
  const oxygen::renderer::vsm::VsmDirectionalClipmapDesc& desc) -> void
{
  if (desc.clip_level_count == 0 || desc.pages_per_axis == 0) {
    throw std::invalid_argument(
      "VsmVirtualAddressSpace: directional clipmaps require non-zero clip "
      "level count and pages-per-axis");
  }

  if (desc.page_grid_origin.size() != desc.clip_level_count
    || desc.page_world_size.size() != desc.clip_level_count
    || desc.near_depth.size() != desc.clip_level_count
    || desc.far_depth.size() != desc.clip_level_count) {
    throw std::invalid_argument(
      "VsmVirtualAddressSpace: directional clipmap descriptors must provide "
      "one origin, world size, and depth range per clip level");
  }
}

} // namespace

namespace oxygen::renderer::vsm {

VsmVirtualAddressSpace::~VsmVirtualAddressSpace() = default;

auto VsmVirtualAddressSpace::BeginFrame(
  const VsmVirtualAddressSpaceConfig& config,
  const std::uint64_t frame_generation) -> void
{
  ValidateFrameConfig(config);
  build_state_ = {};
  build_state_.frame.frame_generation = frame_generation;
  build_state_.frame.config = config;
  build_state_.next_virtual_id = config.first_virtual_id;
  DLOG_F(2, "begin frame generation={} first_virtual_id={} debug_name=`{}`",
    frame_generation, config.first_virtual_id, config.debug_name);
}

auto VsmVirtualAddressSpace::AllocateSinglePageLocalLight(
  const VsmSinglePageLightDesc& desc) -> VsmVirtualMapLayout
{
  const auto layout = VsmVirtualMapLayout {
    .id = build_state_.next_virtual_id++,
    .remap_key = desc.remap_key,
    .level_count = 1,
    .pages_per_level_x = 1,
    .pages_per_level_y = 1,
    .total_page_count = 1,
    .first_page_table_entry = build_state_.next_page_table_entry,
  };
  build_state_.next_page_table_entry += layout.total_page_count;
  build_state_.frame.total_page_table_entry_count
    = build_state_.next_page_table_entry;
  build_state_.frame.local_light_layouts.push_back(layout);
  if (layout.remap_key.empty()) {
    DLOG_F(3,
      "allocated single-page local light id={} without remap key; reuse will "
      "be disabled",
      layout.id);
  }
  DLOG_F(3,
    "allocated single-page local light id={} remap_key=`{}` page_entry={}",
    layout.id, layout.remap_key, layout.first_page_table_entry);
  return layout;
}

auto VsmVirtualAddressSpace::AllocatePagedLocalLight(
  const VsmLocalLightDesc& desc) -> VsmVirtualMapLayout
{
  ValidatePagedLocalLightDesc(desc);

  const auto total_page_count
    = desc.level_count * desc.pages_per_level_x * desc.pages_per_level_y;
  const auto layout = VsmVirtualMapLayout {
    .id = build_state_.next_virtual_id++,
    .remap_key = desc.remap_key,
    .level_count = desc.level_count,
    .pages_per_level_x = desc.pages_per_level_x,
    .pages_per_level_y = desc.pages_per_level_y,
    .total_page_count = total_page_count,
    .first_page_table_entry = build_state_.next_page_table_entry,
  };
  build_state_.next_page_table_entry += total_page_count;
  build_state_.frame.total_page_table_entry_count
    = build_state_.next_page_table_entry;
  build_state_.frame.local_light_layouts.push_back(layout);
  if (layout.remap_key.empty()) {
    DLOG_F(3,
      "allocated paged local light id={} without remap key; reuse will be "
      "disabled",
      layout.id);
  }
  DLOG_F(3,
    "allocated paged local light id={} remap_key=`{}` levels={} pages={}x{} "
    "total_pages={} first_page_entry={}",
    layout.id, layout.remap_key, layout.level_count, layout.pages_per_level_x,
    layout.pages_per_level_y, layout.total_page_count,
    layout.first_page_table_entry);
  return layout;
}

auto VsmVirtualAddressSpace::AllocateDirectionalClipmap(
  const VsmDirectionalClipmapDesc& desc) -> VsmClipmapLayout
{
  ValidateDirectionalClipmapDesc(desc);

  const auto layout = VsmClipmapLayout {
    .first_id = build_state_.next_virtual_id,
    .remap_key = desc.remap_key,
    .clip_level_count = desc.clip_level_count,
    .pages_per_axis = desc.pages_per_axis,
    .first_page_table_entry = build_state_.next_page_table_entry,
    .page_grid_origin = desc.page_grid_origin,
    .page_world_size = desc.page_world_size,
    .near_depth = desc.near_depth,
    .far_depth = desc.far_depth,
  };
  build_state_.next_virtual_id += desc.clip_level_count;
  build_state_.next_page_table_entry += TotalPageCount(layout);
  build_state_.frame.total_page_table_entry_count
    = build_state_.next_page_table_entry;
  build_state_.frame.directional_layouts.push_back(layout);
  if (layout.remap_key.empty()) {
    DLOG_F(3,
      "allocated clipmap first_id={} without remap key; reuse will be "
      "disabled",
      layout.first_id);
  }
  DLOG_F(3,
    "allocated clipmap first_id={} remap_key=`{}` clip_levels={} "
    "pages_per_axis={} first_page_entry={}",
    layout.first_id, layout.remap_key, layout.clip_level_count,
    layout.pages_per_axis, layout.first_page_table_entry);
  return layout;
}

auto VsmVirtualAddressSpace::DescribeFrame() const
  -> const VsmVirtualAddressSpaceFrame&
{
  return build_state_.frame;
}

auto VsmVirtualAddressSpace::BuildRemapTable(
  const VsmVirtualAddressSpaceFrame& previous_frame) const
  -> VsmVirtualRemapTable
{
  if (previous_frame.config.first_virtual_id == 0) {
    LOG_F(WARNING,
      "previous frame generation={} has invalid first_virtual_id=0",
      previous_frame.frame_generation);
  }

  const auto table = BuildVirtualRemapTable(previous_frame, build_state_.frame);
  DLOG_F(2,
    "built remap table previous_generation={} current_generation={} entries={}",
    previous_frame.frame_generation, build_state_.frame.frame_generation,
    table.entries.size());
  return table;
}

auto VsmVirtualAddressSpace::ComputeClipmapReuse(
  const VsmClipmapLayout& previous_layout,
  const VsmClipmapLayout& current_layout,
  const VsmClipmapReuseConfig& config) const -> VsmClipmapReuseResult
{
  return vsm::ComputeClipmapReuse(previous_layout, current_layout, config);
}

} // namespace oxygen::renderer::vsm
