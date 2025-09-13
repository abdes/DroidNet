//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/UploadPlanner.h>

#include <algorithm>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::engine::upload {

namespace {
  inline auto AlignUp(uint64_t v, uint64_t a) -> uint64_t
  {
    return (v + (a - 1ull)) & ~(a - 1ull);
  }
}

auto UploadPlanner::PlanBuffers(std::span<const UploadRequest> requests,
  const UploadPolicy& policy) -> UploadPlan
{
  UploadPlan plan {};
  if (requests.empty())
    return plan;

  const uint64_t align
    = UploadPolicy::AlignmentPolicy::kBufferCopyAlignment.get();
  // Collect valid buffer requests first (preserving request_index), then
  // sort/group by destination buffer and dst_offset so recording can batch
  // state transitions per resource. Staging offsets (src_offset) are assigned
  // according to this sorted order.
  std::vector<UploadPlan::BufferCopyRegion> regions;
  regions.reserve(requests.size());
  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& r = requests[i];
    if (r.kind != UploadKind::kBuffer)
      continue;
    if (!std::holds_alternative<UploadBufferDesc>(r.desc))
      continue;
    const auto& bdesc = std::get<UploadBufferDesc>(r.desc);
    if (!bdesc.dst || bdesc.size_bytes == 0)
      continue;

    UploadPlan::BufferCopyRegion br;
    br.dst = bdesc.dst;
    br.dst_offset = bdesc.dst_offset;
    br.size = bdesc.size_bytes;
    br.src_offset = 0; // assign after sorting
    br.request_index = i;
    regions.emplace_back(br);
  }

  // Sort by destination identity then by destination offset to maximize
  // coalescing potential and allow single transition per destination.
  std::sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) {
    const auto ap = a.dst.get();
    const auto bp = b.dst.get();
    if (ap == bp) {
      return a.dst_offset < b.dst_offset;
    }
    return ap < bp;
  });

  // Assign contiguous src offsets in sorted order with required alignment.
  uint64_t running = 0;
  for (auto& br : regions) {
    const uint64_t offset = AlignUp(running, align);
    br.src_offset = offset;
    running = offset + br.size;
  }

  // Optional optimization: merge contiguous regions targeting the same dst
  // when both destination offsets and assigned src offsets are contiguous.
  std::vector<UploadPlan::BufferCopyRegion> merged;
  merged.reserve(regions.size());
  for (size_t i = 0; i < regions.size(); ++i) {
    auto cur = regions[i];
    while (i + 1 < regions.size()) {
      const auto& nxt = regions[i + 1];
      const bool same_dst = (cur.dst.get() == nxt.dst.get());
      const bool dst_contig = (cur.dst_offset + cur.size == nxt.dst_offset);
      const bool src_contig = (cur.src_offset + cur.size == nxt.src_offset);
      if (!(same_dst && dst_contig && src_contig)) {
        break;
      }
      // Merge nxt into cur
      cur.size += nxt.size;
      // Keep cur.request_index as the representative for the merged copy.
      ++i;
    }
    merged.emplace_back(cur);
  }

  plan.total_bytes = running;
  plan.buffer_regions = std::move(merged);
  // Touch policy to avoid unused warnings when compiled with empty batch.
  (void)policy;
  return plan;
}

auto UploadPlanner::PlanTexture2D(const UploadTextureDesc& desc,
  std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
  -> UploadPlan
{
  UploadPlan plan {};
  if (!desc.dst)
    return plan;

  const auto& dst_desc = desc.dst->GetDescriptor();
  const auto fmt = dst_desc.format;
  const auto info = oxygen::graphics::detail::GetFormatInfo(fmt);
  const uint64_t row_align
    = UploadPolicy::AlignmentPolicy::kRowPitchAlignment.get();
  const uint64_t place_align
    = UploadPolicy::AlignmentPolicy::kPlacementAlignment.get();

  if (subresources.empty()) {
    const uint32_t w = dst_desc.width;
    const uint32_t h = dst_desc.height;
    const uint64_t blocks_x = (w + info.block_size - 1u) / info.block_size;
    const uint64_t blocks_y = (h + info.block_size - 1u) / info.block_size;
    const uint64_t row_pitch
      = AlignUp(blocks_x * info.bytes_per_block, row_align);
    const uint64_t slice_pitch = row_pitch * blocks_y;

    oxygen::graphics::TextureUploadRegion r;
    r.buffer_offset = 0; // planner-relative; caller adds staging.offset
    r.buffer_row_pitch = row_pitch;
    r.buffer_slice_pitch = slice_pitch;
    r.dst_slice = oxygen::graphics::TextureSlice { .x = 0,
      .y = 0,
      .z = 0,
      .width = static_cast<uint32_t>(-1),
      .height = static_cast<uint32_t>(-1),
      .depth = 1,
      .mip_level = 0,
      .array_slice = 0 };
    r.dst_subresources
      = oxygen::graphics::TextureSubResourceSet { .base_mip_level = 0,
          .num_mip_levels = 1,
          .base_array_slice = 0,
          .num_array_slices = 1 };
    plan.total_bytes = slice_pitch;
    plan.regions.emplace_back(r);
    return plan;
  }

  plan.regions.reserve(subresources.size());
  uint64_t running = 0;
  for (const auto& sr : subresources) {
    const uint32_t mip = sr.mip;
    const uint32_t mip_w = (std::max)(dst_desc.width >> mip, 1u);
    const uint32_t mip_h = (std::max)(dst_desc.height >> mip, 1u);

    // If width/height are 0, it indicates full subresource.
    const bool full = (sr.width == 0u) || (sr.height == 0u);
    const uint32_t region_w = full ? mip_w : sr.width;
    const uint32_t region_h = full ? mip_h : sr.height;

    // Compute block counts for region width/height (handles BC formats too).
    const uint64_t blocks_x
      = (region_w + info.block_size - 1u) / info.block_size;
    const uint64_t blocks_y
      = (region_h + info.block_size - 1u) / info.block_size;
    const uint64_t row_pitch
      = AlignUp(blocks_x * info.bytes_per_block, row_align);
    const uint64_t slice_pitch = row_pitch * blocks_y;

    oxygen::graphics::TextureUploadRegion r;
    r.buffer_offset = AlignUp(running, place_align);
    r.buffer_row_pitch = row_pitch;
    r.buffer_slice_pitch = slice_pitch;
    // Destination region in texels; 0 size means full in API, but we fill.
    r.dst_slice = oxygen::graphics::TextureSlice { .x = full ? 0u : sr.x,
      .y = full ? 0u : sr.y,
      .z = 0u,
      .width = full ? static_cast<uint32_t>(-1) : region_w,
      .height = full ? static_cast<uint32_t>(-1) : region_h,
      .depth = 1u,
      .mip_level = sr.mip,
      .array_slice = sr.array_slice };
    r.dst_subresources
      = oxygen::graphics::TextureSubResourceSet { .base_mip_level = sr.mip,
          .num_mip_levels = 1,
          .base_array_slice = sr.array_slice,
          .num_array_slices = 1 };

    plan.regions.emplace_back(r);
    running = r.buffer_offset + slice_pitch;
  }
  plan.total_bytes = running;
  // Touch policy to avoid unused warnings in translation units where
  // constexpr members are not considered sufficient usage.
  (void)policy;
  return plan;
}

auto UploadPlanner::PlanTexture3D(const UploadTextureDesc& desc,
  std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
  -> UploadPlan
{
  UploadPlan plan {};
  if (!desc.dst)
    return plan;

  const auto& dst_desc = desc.dst->GetDescriptor();
  const auto fmt = dst_desc.format;
  const auto info = oxygen::graphics::detail::GetFormatInfo(fmt);
  const uint64_t row_align
    = UploadPolicy::AlignmentPolicy::kRowPitchAlignment.get();
  const uint64_t place_align
    = UploadPolicy::AlignmentPolicy::kPlacementAlignment.get();

  // Helper to compute per-2D-slice footprint for a given region size
  auto compute_slice = [&](uint32_t region_w, uint32_t region_h) {
    const uint64_t blocks_x
      = (region_w + info.block_size - 1u) / info.block_size;
    const uint64_t blocks_y
      = (region_h + info.block_size - 1u) / info.block_size;
    const uint64_t row_pitch
      = AlignUp(blocks_x * info.bytes_per_block, row_align);
    const uint64_t slice_pitch = row_pitch * blocks_y;
    return std::pair<uint64_t, uint64_t> { row_pitch, slice_pitch };
  };

  // Full upload default (mip 0, all depth)
  if (subresources.empty()) {
    const uint32_t w = dst_desc.width;
    const uint32_t h = dst_desc.height;
    const uint32_t d = dst_desc.depth;
    const auto [row_pitch, slice_pitch] = compute_slice(w, h);
    oxygen::graphics::TextureUploadRegion r;
    r.buffer_offset = 0;
    r.buffer_row_pitch = row_pitch;
    r.buffer_slice_pitch = slice_pitch;
    r.dst_slice = oxygen::graphics::TextureSlice { .x = 0,
      .y = 0,
      .z = 0,
      .width = static_cast<uint32_t>(-1),
      .height = static_cast<uint32_t>(-1),
      .depth = static_cast<uint32_t>(-1),
      .mip_level = 0,
      .array_slice = 0 };
    r.dst_subresources
      = oxygen::graphics::TextureSubResourceSet { .base_mip_level = 0,
          .num_mip_levels = 1,
          .base_array_slice = 0,
          .num_array_slices = 1 };
    plan.total_bytes = static_cast<uint64_t>(slice_pitch) * d;
    plan.regions.emplace_back(r);
    return plan;
  }

  plan.regions.reserve(subresources.size());
  uint64_t running = 0;
  for (const auto& sr : subresources) {
    const uint32_t mip = sr.mip;
    const uint32_t mip_w = (std::max)(dst_desc.width >> mip, 1u);
    const uint32_t mip_h = (std::max)(dst_desc.height >> mip, 1u);
    const uint32_t mip_d = (std::max)(dst_desc.depth >> mip, 1u);
    const bool full_xy = (sr.width == 0u) || (sr.height == 0u);
    const bool full_z = (sr.depth == 0u);
    const uint32_t region_w = full_xy ? mip_w : sr.width;
    const uint32_t region_h = full_xy ? mip_h : sr.height;
    const uint32_t region_d = full_z ? mip_d : sr.depth;

    const auto [row_pitch, slice_pitch] = compute_slice(region_w, region_h);

    oxygen::graphics::TextureUploadRegion r;
    r.buffer_offset = AlignUp(running, place_align);
    r.buffer_row_pitch = row_pitch;
    // For 3D, buffer_slice_pitch is 2D-slice pitch; total bytes multiply by D
    r.buffer_slice_pitch = slice_pitch;
    r.dst_slice = oxygen::graphics::TextureSlice { .x = full_xy ? 0u : sr.x,
      .y = full_xy ? 0u : sr.y,
      .z = full_z ? 0u : sr.z,
      .width = full_xy ? static_cast<uint32_t>(-1) : region_w,
      .height = full_xy ? static_cast<uint32_t>(-1) : region_h,
      .depth = full_z ? static_cast<uint32_t>(-1) : region_d,
      .mip_level = sr.mip,
      .array_slice = sr.array_slice };
    r.dst_subresources
      = oxygen::graphics::TextureSubResourceSet { .base_mip_level = sr.mip,
          .num_mip_levels = 1,
          .base_array_slice = sr.array_slice,
          .num_array_slices = 1 };

    plan.regions.emplace_back(r);
    running = r.buffer_offset + (slice_pitch * region_d);
  }
  plan.total_bytes = running;
  (void)policy;
  return plan;
}

auto UploadPlanner::PlanTextureCube(const UploadTextureDesc& desc,
  std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
  -> UploadPlan
{
  // Treat as 2D array; use PlanTexture2D math and just respect array_slice.
  // We still implement here to make API explicit and allow future divergence.
  UploadPlan plan {};
  if (!desc.dst)
    return plan;

  const auto& dst_desc = desc.dst->GetDescriptor();
  const auto info = oxygen::graphics::detail::GetFormatInfo(dst_desc.format);
  const uint64_t row_align
    = UploadPolicy::AlignmentPolicy::kRowPitchAlignment.get();
  const uint64_t place_align
    = UploadPolicy::AlignmentPolicy::kPlacementAlignment.get();

  auto compute_row_slice = [&](uint32_t w, uint32_t h) {
    const uint64_t blocks_x = (w + info.block_size - 1u) / info.block_size;
    const uint64_t blocks_y = (h + info.block_size - 1u) / info.block_size;
    const uint64_t row_pitch
      = AlignUp(blocks_x * info.bytes_per_block, row_align);
    const uint64_t slice_pitch = row_pitch * blocks_y;
    return std::pair<uint64_t, uint64_t> { row_pitch, slice_pitch };
  };

  if (subresources.empty()) {
    const auto [row_pitch, slice_pitch]
      = compute_row_slice(dst_desc.width, dst_desc.height);
    oxygen::graphics::TextureUploadRegion r;
    r.buffer_offset = 0;
    r.buffer_row_pitch = row_pitch;
    r.buffer_slice_pitch = slice_pitch;
    r.dst_slice = oxygen::graphics::TextureSlice { .x = 0,
      .y = 0,
      .z = 0,
      .width = static_cast<uint32_t>(-1),
      .height = static_cast<uint32_t>(-1),
      .depth = 1,
      .mip_level = 0,
      .array_slice = 0 };
    r.dst_subresources
      = oxygen::graphics::TextureSubResourceSet { .base_mip_level = 0,
          .num_mip_levels = 1,
          .base_array_slice = 0,
          .num_array_slices = 1 };
    plan.total_bytes = slice_pitch;
    plan.regions.emplace_back(r);
    return plan;
  }

  plan.regions.reserve(subresources.size());
  uint64_t running = 0;
  for (const auto& sr : subresources) {
    const uint32_t mip = sr.mip;
    const uint32_t mip_w = (std::max)(dst_desc.width >> mip, 1u);
    const uint32_t mip_h = (std::max)(dst_desc.height >> mip, 1u);
    const bool full_xy = (sr.width == 0u) || (sr.height == 0u);
    const uint32_t region_w = full_xy ? mip_w : sr.width;
    const uint32_t region_h = full_xy ? mip_h : sr.height;
    const auto [row_pitch, slice_pitch] = compute_row_slice(region_w, region_h);

    oxygen::graphics::TextureUploadRegion r;
    r.buffer_offset = AlignUp(running, place_align);
    r.buffer_row_pitch = row_pitch;
    r.buffer_slice_pitch = slice_pitch;
    r.dst_slice = oxygen::graphics::TextureSlice { .x = full_xy ? 0u : sr.x,
      .y = full_xy ? 0u : sr.y,
      .z = 0u,
      .width = full_xy ? static_cast<uint32_t>(-1) : region_w,
      .height = full_xy ? static_cast<uint32_t>(-1) : region_h,
      .depth = 1u,
      .mip_level = sr.mip,
      .array_slice = sr.array_slice };
    r.dst_subresources
      = oxygen::graphics::TextureSubResourceSet { .base_mip_level = sr.mip,
          .num_mip_levels = 1,
          .base_array_slice = sr.array_slice,
          .num_array_slices = 1 };

    plan.regions.emplace_back(r);
    running = r.buffer_offset + slice_pitch;
  }
  plan.total_bytes = running;
  (void)policy;
  return plan;
}

} // namespace oxygen::engine::upload
