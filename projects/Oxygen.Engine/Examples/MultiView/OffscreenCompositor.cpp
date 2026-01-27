//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Texture.h>

#include "MultiView/OffscreenCompositor.h"

namespace oxygen::examples::multiview {
auto OffscreenCompositor::CompositeFullscreen(
  graphics::CommandRecorder& recorder, graphics::Texture& source_texture,
  graphics::Texture& backbuffer) -> void
{
  LOG_F(INFO, "[Compositor] Fullscreen: src={}x{} -> dst={}x{}",
    source_texture.GetDescriptor().width, source_texture.GetDescriptor().height,
    backbuffer.GetDescriptor().width, backbuffer.GetDescriptor().height);

  // Track source texture state
  recorder.BeginTrackingResourceState(
    source_texture, graphics::ResourceStates::kCommon);

  // Transition source to copy source
  recorder.RequireResourceState(
    source_texture, graphics::ResourceStates::kCopySource);

  // Transition backbuffer to copy dest
  recorder.RequireResourceState(
    backbuffer, graphics::ResourceStates::kCopyDest);

  recorder.FlushBarriers();

  // Copy as much as fits in both source and destination. CopyTextureRegion
  // requires the copied region to be wholly inside both resources; doing a
  // conservative min() clamp prevents the destination-out-of-bounds errors
  // seen when sizes don't match exactly.
  const auto& src_desc = source_texture.GetDescriptor();
  const auto& dst_desc = backbuffer.GetDescriptor();

  const uint32_t copy_w = std::min(src_desc.width, dst_desc.width);
  const uint32_t copy_h = std::min(src_desc.height, dst_desc.height);

  const graphics::TextureSlice src_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = copy_w,
    .height = copy_h,
    .depth = 1,
  };

  const graphics::TextureSlice dst_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = copy_w,
    .height = copy_h,
    .depth = 1,
  };

  constexpr graphics::TextureSubResourceSet subresources {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  recorder.CopyTexture(source_texture, src_slice, subresources, backbuffer,
    dst_slice, subresources);
  // After copying, the source texture is left in kCopySource; restore it to
  // a neutral/common state so subsequent render passes (which expect the
  // texture to be CPU/GPU common or render-target-ready) don't see a
  // mismatched prior state when BeginTrackingResourceState is called.
  recorder.RequireResourceState(
    source_texture, graphics::ResourceStates::kCommon);
  recorder.FlushBarriers();
  LOG_F(INFO, "[Compositor] Fullscreen composite complete");
}

auto OffscreenCompositor::CompositeToRegion(graphics::CommandRecorder& recorder,
  graphics::Texture& source_texture, graphics::Texture& backbuffer,
  const ViewPort& viewport) -> void
{
  LOG_F(INFO, "[Compositor] Region: src={}x{} -> dst region ({},{}) {}x{}",
    source_texture.GetDescriptor().width, source_texture.GetDescriptor().height,
    static_cast<int>(viewport.top_left_x),
    static_cast<int>(viewport.top_left_y), static_cast<int>(viewport.width),
    static_cast<int>(viewport.height));

  // Track source texture state
  recorder.BeginTrackingResourceState(
    source_texture, graphics::ResourceStates::kCommon);

  // Transition source to copy source
  recorder.RequireResourceState(
    source_texture, graphics::ResourceStates::kCopySource);

  // Backbuffer already in kCopyDest from previous composite
  recorder.RequireResourceState(
    backbuffer, graphics::ResourceStates::kCopyDest);

  recorder.FlushBarriers();

  // Copy source to viewport region
  // The desired destination rectangle must be inside the backbuffer and the
  // source box must be inside the source texture. We clip the region so that
  // both src and dst are guaranteed valid; CopyTextureRegion does not scale
  // so we copy the largest overlapping rectangle starting at 0,0 in source
  // into the destination position.
  const auto& s_desc = source_texture.GetDescriptor();
  const auto& d_desc = backbuffer.GetDescriptor();

  const uint32_t dst_x = static_cast<uint32_t>(
    std::clamp(viewport.top_left_x, 0.0F, static_cast<float>(d_desc.width)));
  const uint32_t dst_y = static_cast<uint32_t>(
    std::clamp(viewport.top_left_y, 0.0F, static_cast<float>(d_desc.height)));

  // maximum available width/height at destination starting at dst_x/y
  const uint32_t max_dst_w = d_desc.width > dst_x ? d_desc.width - dst_x : 0U;
  const uint32_t max_dst_h = d_desc.height > dst_y ? d_desc.height - dst_y : 0U;

  // source available sizes
  const uint32_t max_src_w = s_desc.width;
  const uint32_t max_src_h = s_desc.height;

  const uint32_t copy_width = std::min(max_src_w, max_dst_w);
  const uint32_t copy_height = std::min(max_src_h, max_dst_h);

  if (copy_width == 0 || copy_height == 0) {
    LOG_F(INFO,
      "[Compositor] Skipping region copy, clipped size is 0 (dst {}x{} at "
      "{}x{})",
      d_desc.width, d_desc.height, dst_x, dst_y);
    return;
  }

  const graphics::TextureSlice src_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = copy_width,
    .height = copy_height,
    .depth = 1,
  };

  const graphics::TextureSlice dst_slice {
    .x = dst_x,
    .y = dst_y,
    .z = 0,
    .width = copy_width,
    .height = copy_height,
    .depth = 1,
  };

  constexpr graphics::TextureSubResourceSet src_subresources {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  recorder.CopyTexture(source_texture, src_slice, src_subresources, backbuffer,
    dst_slice, src_subresources);
  // See comment in CompositeFullscreen: reset the source state back to Common
  // so future command lists don't encounter an unexpected COPY_SOURCE state
  // in the resource state tracking system.
  recorder.RequireResourceState(
    source_texture, graphics::ResourceStates::kCommon);
  recorder.FlushBarriers();
  LOG_F(INFO, "[Compositor] Region composite complete");
}

} // namespace oxygen::examples::multiview
