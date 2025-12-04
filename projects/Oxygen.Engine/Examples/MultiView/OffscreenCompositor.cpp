//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "OffscreenCompositor.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Texture.h>

using namespace oxygen;
using namespace oxygen::examples::multiview;

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

  // Copy entire texture
  const graphics::TextureSlice src_slice { .x = 0,
    .y = 0,
    .z = 0,
    .width = source_texture.GetDescriptor().width,
    .height = source_texture.GetDescriptor().height,
    .depth = 1 };

  const graphics::TextureSlice dst_slice { .x = 0,
    .y = 0,
    .z = 0,
    .width = backbuffer.GetDescriptor().width,
    .height = backbuffer.GetDescriptor().height,
    .depth = 1 };

  const graphics::TextureSubResourceSet subresources { .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1 };

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
  const graphics::TextureSlice src_slice { .x = 0,
    .y = 0,
    .z = 0,
    .width = source_texture.GetDescriptor().width,
    .height = source_texture.GetDescriptor().height,
    .depth = 1 };

  const graphics::TextureSlice dst_slice { .x
    = static_cast<uint32_t>(viewport.top_left_x),
    .y = static_cast<uint32_t>(viewport.top_left_y),
    .z = 0,
    .width = static_cast<uint32_t>(viewport.width),
    .height = static_cast<uint32_t>(viewport.height),
    .depth = 1 };

  const graphics::TextureSubResourceSet subresources { .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1 };

  recorder.CopyTexture(source_texture, src_slice, subresources, backbuffer,
    dst_slice, subresources);
  // See comment in CompositeFullscreen: reset the source state back to Common
  // so future command lists don't encounter an unexpected COPY_SOURCE state
  // in the resource state tracking system.
  recorder.RequireResourceState(
    source_texture, graphics::ResourceStates::kCommon);
  recorder.FlushBarriers();
  LOG_F(INFO, "[Compositor] Region composite complete");
}
