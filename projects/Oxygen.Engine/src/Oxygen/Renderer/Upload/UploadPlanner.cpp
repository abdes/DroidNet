//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <optional>
#include <tuple>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/UploadPlanner.h>

using oxygen::graphics::TextureUploadRegion;
using oxygen::graphics::detail::FormatInfo;

namespace {

using oxygen::engine::upload::BufferUploadPlan;
using oxygen::engine::upload::TextureUploadPlan;
using oxygen::engine::upload::UploadError;

inline auto AlignUp(uint64_t v, uint64_t a) -> uint64_t
{
  return (v + (a - 1ULL)) & ~(a - 1ULL);
}

// Helper to compute per-2D-slice footprint for a given region size
auto ComputeSlice(const FormatInfo& info, uint32_t region_w, uint32_t region_h,
  uint64_t row_align)
{
  const auto blocks_x = (region_w + info.block_size - 1U) / info.block_size;
  const auto blocks_y = (region_h + info.block_size - 1U) / info.block_size;
  const auto row_pitch = AlignUp(blocks_x * info.bytes_per_block, row_align);
  const auto slice_pitch = row_pitch * blocks_y;
  return std::pair<uint64_t, uint64_t> { row_pitch, slice_pitch };
};


using oxygen::graphics::BufferUploadRegion;
using oxygen::engine::upload::UploadItem;

// Use AlignUp defined earlier in this translation unit.

// Validate a buffer request and extract destination buffer and size/offset
struct ValidBufReq {
  std::shared_ptr<oxygen::graphics::Buffer> dst;
  uint64_t dst_offset;
  uint64_t size;
};

inline auto TryValidateBufReq(const oxygen::engine::upload::UploadRequest& r)
  -> std::optional<ValidBufReq>
{
  if (r.kind != oxygen::engine::upload::UploadKind::kBuffer) {
    return std::nullopt;
  }
  if (!std::holds_alternative<oxygen::engine::upload::UploadBufferDesc>(r.desc)) {
    LOG_F(WARNING, "-skip- request is for a buffer upload, but desc is not");
    return std::nullopt;
  }
  const auto& bdesc
    = std::get<oxygen::engine::upload::UploadBufferDesc>(r.desc);
  if (!bdesc.dst || bdesc.size_bytes == 0) {
    LOG_F(WARNING, "-skip- null or empty upload destination");
    return std::nullopt;
  }
  const auto bound = bdesc.dst_offset + bdesc.size_bytes;
  if (bound > bdesc.dst->GetDescriptor().size_bytes) {
    LOG_F(WARNING, "-skip- request would overflow destination buffer");
    return std::nullopt;
  }
  return ValidBufReq { bdesc.dst, bdesc.dst_offset, bdesc.size_bytes };
}

struct SortKey {
  const void* dst_ptr;
  uint64_t dst_offset;
};

inline auto MakeSortKey(const ValidBufReq& v) -> SortKey
{
  return SortKey { v.dst.get(), v.dst_offset };
}

//! Build a TextureUploadPlan or return an UploadError when the computed
//! regions are empty.
inline auto MakeTexturePlanOrError(
  uint64_t total_size, std::vector<TextureUploadRegion>&& regions,
  std::vector<std::size_t>&& source_indices)
  -> std::expected<TextureUploadPlan, UploadError>
{
  // We have requests, but none were valid. This is something we cannot
  // gracefully ignore.
  if (regions.empty()) {
    DLOG_F(ERROR, "-failed- no upload request was valid");
    return std::unexpected(UploadError::kInvalidRequest);
  }
  DCHECK_F(regions.size() == source_indices.size(),
    "texture plan mapping must match regions");
  DLOG_F(
    2, "plan summary: {} regions, {} bytes total", regions.size(), total_size);
  return TextureUploadPlan {
    .total_bytes = total_size,
    .regions = std::move(regions),
    .source_indices = std::move(source_indices),
  };
}

} // anonymous namespace

namespace oxygen::engine::upload {

  auto UploadPlanner::PlanBuffers(std::span<const UploadRequest> requests,
  const UploadPolicy& policy) -> std::expected<BufferUploadPlan, UploadError>
{
  DLOG_SCOPE_FUNCTION(2);
  const uint64_t align = policy.alignment.buffer_copy_alignment.get();

  BufferUploadPlan plan;
  if (requests.empty()) {
    return plan; // empty plan
  }

  struct IndexedValid {
    size_t index;
    ValidBufReq valid;
  };

  std::vector<IndexedValid> valid;
  valid.reserve(requests.size());
  for (size_t idx = 0; idx < requests.size(); ++idx) {
    const auto& r = requests[idx];
    if (auto v = TryValidateBufReq(r)) {
      valid.push_back(IndexedValid { idx, *v });
    }
  }

  if (valid.empty()) {
    DLOG_F(ERROR, "-failed- no upload request was valid");
    return std::unexpected(UploadError::kInvalidRequest);
  }

  // Build deterministic ordering for destination groups based on first
  // occurrence of the destination in the input request list. This ensures
  // planner output is stable and does not depend on heap pointer ordering.
  std::unordered_map<const void*, size_t> first_occurrence;
  first_occurrence.reserve(valid.size());
  for (const auto& iv : valid) {
    const void* ptr = iv.valid.dst.get();
    auto it = first_occurrence.find(ptr);
    if (it == first_occurrence.end()) {
      first_occurrence.emplace(ptr, iv.index);
    } else if (iv.index < it->second) {
      it->second = iv.index;
    }
  }

  std::sort(valid.begin(), valid.end(), [&](const auto& a, const auto& b) {
    const auto ka = MakeSortKey(a.valid);
    const auto kb = MakeSortKey(b.valid);
    const auto fa = first_occurrence[ka.dst_ptr];
    const auto fb = first_occurrence[kb.dst_ptr];
    if (fa != fb) {
      return fa < fb; // group order by first appearance
    }
    if (ka.dst_ptr == kb.dst_ptr) {
      return ka.dst_offset < kb.dst_offset;
    }
    // Fallback to pointer ordering for stability when first-occurrence ties
    return ka.dst_ptr < kb.dst_ptr;
  });

  uint64_t running = 0;
  plan.uploads.reserve(valid.size());
  for (const auto& iv : valid) {
    const uint64_t src = AlignUp(running, align);
    running = src + iv.valid.size;
    UploadItem item {
      .region = BufferUploadRegion {
        .dst_offset = iv.valid.dst_offset,
        .src_offset = src,
        .size = iv.valid.size,
      },
      .request_indices = std::vector<std::size_t> { iv.index },
    };
    plan.uploads.emplace_back(std::move(item));
  }
  plan.total_bytes = running;
  DLOG_F(2, "plan summary: {} regions, {} bytes total", plan.uploads.size(), plan.total_bytes);
  return plan;
}

auto UploadPlanner::OptimizeBuffers(std::span<const UploadRequest> requests,
  const BufferUploadPlan& plan, const UploadPolicy& /*policy*/)
  -> std::expected<BufferUploadPlan, UploadError>
{
  DLOG_SCOPE_FUNCTION(2);
  BufferUploadPlan out;
  if (plan.uploads.empty()) {
    return out; // nothing to optimize
  }
  out.total_bytes = plan.total_bytes;
  out.uploads.reserve(plan.uploads.size());

  auto cur = plan.uploads.front();
  for (size_t i = 1; i < plan.uploads.size(); ++i) {
    const auto& nxt = plan.uploads[i];
    // Same destination? Compare using representative request index
    const auto rep_cur = cur.request_indices.front();
    const auto rep_nxt = nxt.request_indices.front();
    const auto& rc = requests[rep_cur];
    const auto& rn = requests[rep_nxt];
    const auto& bdc = std::get<UploadBufferDesc>(rc.desc);
    const auto& bdn = std::get<UploadBufferDesc>(rn.desc);
    const bool same_dst = (bdc.dst.get() == bdn.dst.get());

    const bool dst_contig
      = (cur.region.dst_offset + cur.region.size) == nxt.region.dst_offset;
    const bool src_contig
      = (cur.region.src_offset + cur.region.size) == nxt.region.src_offset;
    if (!(same_dst && dst_contig && src_contig)) {
      out.uploads.emplace_back(std::move(cur));
      cur = nxt;
      continue;
    }
    // Merge nxt into cur
    cur.region.size += nxt.region.size;
    cur.request_indices.insert(cur.request_indices.end(),
      nxt.request_indices.begin(), nxt.request_indices.end());
  }
  out.uploads.emplace_back(std::move(cur));
  DLOG_F(2, "opt summary: {} regions (from {}), {} bytes total", out.uploads.size(), plan.uploads.size(), out.total_bytes);
  return out;
}

auto UploadPlanner::PlanTexture2D(const UploadTextureDesc& desc,
  std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
  -> std::expected<TextureUploadPlan, UploadError>
{
  /*!
   Plans a Texture2D upload into a single staging allocation.

   This method follows common modern engine practice (D3D12/Vulkan): the
   planner is authoritative for the **destination staging layout**, while the
   request provides the **destination subresource/box** list (mip/array +
   optional x/y/width/height).

   The staging layout is planned in format block-space:

   - blocks_x = ceil(region_width / block_size)
   - blocks_y = ceil(region_height / block_size)
   - bytes_per_row = blocks_x * bytes_per_block
   - buffer_row_pitch = AlignUp(bytes_per_row, policy.alignment.row_pitch)
   - buffer_offset = AlignUp(previous_end, policy.alignment.placement)

   Best practices implemented here:

   - **Policy-driven alignment**: All alignment is derived from UploadPolicy.
   - **Deterministic ordering**: Regions are sorted by (array_slice, mip, y, x)
     before packing; this makes plans stable for debugging and repro.
   - **Concrete dimensions**: Planned regions always carry concrete
     width/height (no sentinel max values).
   - **BC correctness**: For block-compressed formats, partial boxes must be
     aligned to the block size. Full-subresource uploads are allowed even if
     the mip dimensions are not multiples of the block size.
   - **Source mapping**: The returned plan includes source_indices mapping so
     the coordinator can pack per-subresource source layouts into staging.
  */
  if (!desc.dst) {
    return {};
  }

  DLOG_SCOPE_FUNCTION(2);

  // Programming logic error. Should not happen if used correctly.
  DCHECK_NOTNULL_F(desc.dst);

  const auto& dst_desc = desc.dst->GetDescriptor();
  DLOG_F(2, "dst: {}x{} format={} subresources={}", dst_desc.width,
    dst_desc.height, static_cast<int>(dst_desc.format), subresources.size());

  // Fatal if descriptor is not valid
  if (dst_desc.width == 0 || dst_desc.height == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  // Fatal if format info is invalid
  const auto info = oxygen::graphics::detail::GetFormatInfo(dst_desc.format);
  if (info.bytes_per_block == 0 || info.block_size == 0) {
    DLOG_F(ERROR, "unsupported or invalid texture format: {}", dst_desc.format);
    return std::unexpected(UploadError::kInvalidRequest);
  }

  const uint64_t row_align = policy.alignment.row_pitch_alignment.get();
  const uint64_t place_align = policy.alignment.placement_alignment.get();

  if (subresources.empty()) {
    const auto [row_pitch, slice_pitch]
      = ComputeSlice(info, dst_desc.width, dst_desc.height, row_align);

    oxygen::graphics::TextureUploadRegion r{
      .buffer_offset = 0, // planner-relative; caller adds staging.offset
      .buffer_row_pitch = row_pitch,
      .buffer_slice_pitch = slice_pitch,
      .dst_slice = oxygen::graphics::TextureSlice{
        .x = 0,
        .y = 0,
        .z = 0,
        .width = dst_desc.width,
        .height = dst_desc.height,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
      .dst_subresources = oxygen::graphics::TextureSubResourceSet{
        .base_mip_level = 0,
        .num_mip_levels = 1,
        .base_array_slice = 0,
        .num_array_slices = 1,
      },
    };
    return TextureUploadPlan {
      .total_bytes = slice_pitch,
      .regions = { r },
      .source_indices = { 0 },
    };
  }

  struct Planned {
    UploadSubresource sr;
    uint32_t region_w { 0 };
    uint32_t region_h { 0 };
    uint64_t row_pitch { 0 };
    uint64_t slice_pitch { 0 };
    std::size_t source_index { 0 };
  };

  std::vector<Planned> planned;
  planned.reserve(subresources.size());
  for (std::size_t idx = 0; idx < subresources.size(); ++idx) {
    const auto& sr = subresources[idx];
    DLOG_SCOPE_F(3, fmt::format("subresource[{}]", idx).c_str());
    DLOG_F(3, "mip={}, array_slice={}", sr.mip, sr.array_slice);
    DLOG_F(3, "x,y={},{} w,h={},{}", sr.x, sr.y, sr.width, sr.height);

    const uint32_t mip = sr.mip;
    if (mip >= dst_desc.mip_levels) {
      LOG_F(WARNING, "-skip- subresource mip is out of range (mips={})",
        dst_desc.mip_levels);
      continue;
    }
    if (sr.array_slice >= dst_desc.array_size) {
      LOG_F(WARNING,
        "-skip- subresource array slice is out of range (arrays={})",
        dst_desc.array_size);
      continue;
    }

    const uint32_t mip_w = (std::max)(dst_desc.width >> mip, 1U);
    const uint32_t mip_h = (std::max)(dst_desc.height >> mip, 1U);

    const bool full = (sr.width == 0U) || (sr.height == 0U);
    const uint32_t region_w = full ? mip_w : sr.width;
    const uint32_t region_h = full ? mip_h : sr.height;

    if (!full && (sr.x + region_w > mip_w || sr.y + region_h > mip_h)) {
      LOG_F(WARNING, "-skip- subresource region out of bounds");
      continue;
    }

    if (info.block_size > 1U) {
      DLOG_F(3, "subresource uses BC format (block size {})", info.block_size);
      if (!full
        && (((sr.x % info.block_size) != 0U)
          || ((sr.y % info.block_size) != 0U)
          || ((region_w % info.block_size) != 0U)
          || ((region_h % info.block_size) != 0U))) {
        LOG_F(WARNING, "-skip- subresource not aligned to block size");
        continue;
      }
    }

    const auto [row_pitch, slice_pitch]
      = ComputeSlice(info, region_w, region_h, row_align);
    planned.push_back({
      .sr = sr,
      .region_w = region_w,
      .region_h = region_h,
      .row_pitch = row_pitch,
      .slice_pitch = slice_pitch,
      .source_index = idx,
    });
  }

  if (planned.empty()) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  std::sort(planned.begin(), planned.end(), [](const Planned& a, const Planned& b) {
    const auto ak = std::tuple { a.sr.array_slice, a.sr.mip, a.sr.y, a.sr.x };
    const auto bk = std::tuple { b.sr.array_slice, b.sr.mip, b.sr.y, b.sr.x };
    return ak < bk;
  });

  std::vector<TextureUploadRegion> regions;
  std::vector<std::size_t> source_indices;
  regions.reserve(planned.size());
  source_indices.reserve(planned.size());

  uint64_t running = 0;
  for (const auto& p : planned) {
    const auto offset = AlignUp(running, place_align);
    oxygen::graphics::TextureUploadRegion r{
      .buffer_offset = offset,
      .buffer_row_pitch = p.row_pitch,
      .buffer_slice_pitch = p.slice_pitch,
      .dst_slice = oxygen::graphics::TextureSlice{
        .x = p.sr.x,
        .y = p.sr.y,
        .z = 0U,
        .width = p.region_w,
        .height = p.region_h,
        .depth = 1U,
        .mip_level = p.sr.mip,
        .array_slice = p.sr.array_slice,
      },
      .dst_subresources = oxygen::graphics::TextureSubResourceSet{
        .base_mip_level = p.sr.mip,
        .num_mip_levels = 1,
        .base_array_slice = p.sr.array_slice,
        .num_array_slices = 1,
      },
    };
    regions.emplace_back(r);
    source_indices.emplace_back(p.source_index);
    running = offset + p.slice_pitch;
  }

  return MakeTexturePlanOrError(
    running, std::move(regions), std::move(source_indices));
}

auto UploadPlanner::PlanTexture3D(const UploadTextureDesc& desc,
  std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
  -> std::expected<TextureUploadPlan, UploadError>
{
  if (!desc.dst) {
    return {};
  }

  DLOG_SCOPE_FUNCTION(2);

  // Programming logic error. Should not happen if used correctly.
  DCHECK_NOTNULL_F(desc.dst);

  const auto& dst_desc = desc.dst->GetDescriptor();
  DLOG_F(2, "dst: {}x{}x{} format={} subresources={}", dst_desc.width,
    dst_desc.height, dst_desc.depth, static_cast<int>(dst_desc.format),
    subresources.size());

  if (dst_desc.width == 0 || dst_desc.height == 0 || dst_desc.depth == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  if (dst_desc.depth == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  const auto info = oxygen::graphics::detail::GetFormatInfo(dst_desc.format);
  if (info.bytes_per_block == 0 || info.block_size == 0) {
    DLOG_F(ERROR, "unsupported or invalid texture format: {}", dst_desc.format);
    return std::unexpected(UploadError::kInvalidRequest);
  }

  const uint64_t row_align = policy.alignment.row_pitch_alignment.get();
  const uint64_t place_align = policy.alignment.placement_alignment.get();

  // Full upload default (mip 0, all depth)
  if (subresources.empty()) {
    const auto [row_pitch, slice_pitch]
      = ComputeSlice(info, dst_desc.width, dst_desc.height, row_align);
    oxygen::graphics::TextureUploadRegion r{
      .buffer_offset = 0,
      .buffer_row_pitch = row_pitch,
      .buffer_slice_pitch = slice_pitch,
      .dst_slice = oxygen::graphics::TextureSlice{
        .x = 0,
        .y = 0,
        .z = 0,
        .width = dst_desc.width,
        .height = dst_desc.height,
        .depth = dst_desc.depth,
        .mip_level = 0,
        .array_slice = 0,
      },
      .dst_subresources = oxygen::graphics::TextureSubResourceSet{
        .base_mip_level = 0,
        .num_mip_levels = 1,
        .base_array_slice = 0,
        .num_array_slices = 1,
      },
    };
    return TextureUploadPlan {
      .total_bytes = slice_pitch * dst_desc.depth,
      .regions = { r },
      .source_indices = { 0 },
    };
  }

  struct Planned3D {
    UploadSubresource sr;
    uint32_t region_w { 0 };
    uint32_t region_h { 0 };
    uint32_t region_d { 0 };
    uint64_t row_pitch { 0 };
    uint64_t slice_pitch { 0 };
    std::size_t source_index { 0 };
  };

  std::vector<Planned3D> planned;
  planned.reserve(subresources.size());
  for (std::size_t idx = 0; idx < subresources.size(); ++idx) {
    const auto& sr = subresources[idx];
    DLOG_SCOPE_F(3, fmt::format("subresource[{}]", idx).c_str());
    DLOG_F(3, "mip={}, array_slice={}", sr.mip, sr.array_slice);
    DLOG_F(3, "x,y={},{} w,h={},{}", sr.x, sr.y, sr.width, sr.height);
    DLOG_F(3, "z={}, depth={}", sr.z, sr.depth);

    const uint32_t mip = sr.mip;
    if (mip >= dst_desc.mip_levels) {
      LOG_F(WARNING, "-skip- subresource is out of range (mips={})",
        dst_desc.mip_levels);
      continue;
    }
    if (sr.array_slice >= dst_desc.array_size) {
      LOG_F(WARNING,
        "-skip- subresource array slice is out of range (arrays={})",
        dst_desc.array_size);
      continue;
    }

    const auto mip_w = (std::max)(dst_desc.width >> mip, 1U);
    const auto mip_h = (std::max)(dst_desc.height >> mip, 1U);
    const auto mip_d = (std::max)(dst_desc.depth >> mip, 1U);
    const bool full_xy = (sr.width == 0U) || (sr.height == 0U);
    const bool full_z = (sr.depth == 0U);
    const auto region_w = full_xy ? mip_w : sr.width;
    const auto region_h = full_xy ? mip_h : sr.height;
    const auto region_d = full_z ? mip_d : sr.depth;

    if (!full_xy && (sr.x + region_w > mip_w || sr.y + region_h > mip_h)) {
      LOG_F(WARNING,
        "-skip- subresource region out of bounds (mip {} size {}x{})", mip,
        mip_w, mip_h);
      continue;
    }
    if (!full_z && (sr.z + region_d > mip_d)) {
      LOG_F(WARNING, "-skip- subresource depth out of bounds");
      continue;
    }

    if (info.block_size > 1U) {
      DLOG_F(3, "subresource uses BC format (block size {})", info.block_size);
      if (!full_xy
        && (((sr.x % info.block_size) != 0U)
          || ((sr.y % info.block_size) != 0U)
          || ((region_w % info.block_size) != 0U)
          || ((region_h % info.block_size) != 0U))) {
        LOG_F(WARNING, "-skip- subresource not aligned to block size");
        continue;
      }
    }

    const auto [row_pitch, slice_pitch]
      = ComputeSlice(info, region_w, region_h, row_align);
    planned.push_back({
      .sr = sr,
      .region_w = region_w,
      .region_h = region_h,
      .region_d = region_d,
      .row_pitch = row_pitch,
      .slice_pitch = slice_pitch,
      .source_index = idx,
    });
  }

  if (planned.empty()) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  std::sort(planned.begin(), planned.end(), [](const Planned3D& a, const Planned3D& b) {
    const auto ak = std::tuple { a.sr.array_slice, a.sr.mip, a.sr.z, a.sr.y, a.sr.x };
    const auto bk = std::tuple { b.sr.array_slice, b.sr.mip, b.sr.z, b.sr.y, b.sr.x };
    return ak < bk;
  });

  std::vector<TextureUploadRegion> regions;
  std::vector<std::size_t> source_indices;
  regions.reserve(planned.size());
  source_indices.reserve(planned.size());

  uint64_t running = 0;
  for (const auto& p : planned) {
    const auto offset = AlignUp(running, place_align);
    oxygen::graphics::TextureUploadRegion r{
      .buffer_offset = offset,
      .buffer_row_pitch = p.row_pitch,
      .buffer_slice_pitch = p.slice_pitch,
      .dst_slice = oxygen::graphics::TextureSlice{
        .x = p.sr.x,
        .y = p.sr.y,
        .z = p.sr.z,
        .width = p.region_w,
        .height = p.region_h,
        .depth = p.region_d,
        .mip_level = p.sr.mip,
        .array_slice = p.sr.array_slice,
      },
      .dst_subresources = oxygen::graphics::TextureSubResourceSet{
        .base_mip_level = p.sr.mip,
        .num_mip_levels = 1,
        .base_array_slice = p.sr.array_slice,
        .num_array_slices = 1,
      },
    };
    regions.emplace_back(r);
    source_indices.emplace_back(p.source_index);
    running = offset + (p.slice_pitch * p.region_d);
  }

  return MakeTexturePlanOrError(
    running, std::move(regions), std::move(source_indices));
}

} // namespace oxygen::engine::upload
