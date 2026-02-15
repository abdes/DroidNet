//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>

#include "DemoShell/Runtime/Internal/CompositionViewImpl.h"

namespace oxygen::examples::internal {

void CompositionViewImpl::PrepareForRender(const CompositionView& descriptor,
  uint32_t submission_order, frame::SequenceNumber frame_seq,
  Graphics& graphics, ViewLifecycleAccessTag tag)
{
  descriptor_ = descriptor;
  submission_order_ = submission_order;
  last_seen_frame_ = frame_seq;
  EnsureResources(graphics, tag);
}

void CompositionViewImpl::EnsureResources(
  Graphics& graphics, ViewLifecycleAccessTag /*tag*/)
{
  const uint32_t target_w
    = std::max(1U, static_cast<uint32_t>(descriptor_.view.viewport.width));
  const uint32_t target_h
    = std::max(1U, static_cast<uint32_t>(descriptor_.view.viewport.height));
  const bool needs_hdr = descriptor_.enable_hdr;
  const graphics::Color& target_clear = descriptor_.clear_color;

  if (render_target_width_ == target_w && render_target_height_ == target_h
    && uses_hdr_render_targets_ == needs_hdr && clear_color_ == target_clear) {
    if (needs_hdr && hdr_texture_) {
      return;
    }
    if (!needs_hdr && sdr_texture_) {
      return;
    }
  }

  LOG_F(INFO,
    "Configuring View '{}' (ID: {}) -> {}x{}, HDR: {}, "
    "Clear: ({}, {}, {}, {})",
    descriptor_.name, descriptor_.id, target_w, target_h, needs_hdr,
    target_clear.r, target_clear.g, target_clear.b, target_clear.a);

  render_target_width_ = target_w;
  render_target_height_ = target_h;
  uses_hdr_render_targets_ = needs_hdr;
  clear_color_ = target_clear;

  if (needs_hdr) {
    graphics::TextureDesc hdr_desc;
    hdr_desc.width = target_w;
    hdr_desc.height = target_h;
    hdr_desc.format = oxygen::Format::kRGBA16Float;
    hdr_desc.texture_type = oxygen::TextureType::kTexture2D;
    hdr_desc.is_render_target = true;
    hdr_desc.is_shader_resource = true;
    hdr_desc.use_clear_value = true;
    hdr_desc.clear_value = target_clear;
    hdr_desc.initial_state = graphics::ResourceStates::kCommon;
    hdr_desc.debug_name = "Forward_HDR_Intermediate";
    hdr_texture_ = graphics.CreateTexture(hdr_desc);

    graphics::FramebufferDesc hdr_fb_desc;
    hdr_fb_desc.AddColorAttachment({ .texture = hdr_texture_ });

    graphics::TextureDesc depth_desc;
    depth_desc.width = target_w;
    depth_desc.height = target_h;
    depth_desc.format = oxygen::Format::kDepth32;
    depth_desc.texture_type = oxygen::TextureType::kTexture2D;
    depth_desc.is_render_target = true;
    depth_desc.is_shader_resource = true;
    depth_desc.use_clear_value = true;
    depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
    depth_desc.initial_state = graphics::ResourceStates::kCommon;
    depth_desc.debug_name = "Forward_HDR_Depth";
    hdr_fb_desc.SetDepthAttachment(graphics.CreateTexture(depth_desc));
    hdr_framebuffer_ = graphics.CreateFramebuffer(hdr_fb_desc);
  } else {
    hdr_texture_ = nullptr;
    hdr_framebuffer_ = nullptr;
  }

  graphics::TextureDesc sdr_desc;
  sdr_desc.width = target_w;
  sdr_desc.height = target_h;
  sdr_desc.format = oxygen::Format::kRGBA8UNorm;
  sdr_desc.texture_type = oxygen::TextureType::kTexture2D;
  sdr_desc.is_render_target = true;
  sdr_desc.is_shader_resource = true;
  sdr_desc.use_clear_value = true;
  sdr_desc.clear_value = target_clear;
  sdr_desc.initial_state = graphics::ResourceStates::kCommon;
  sdr_desc.debug_name = "Forward_SDR_Intermediate";
  sdr_texture_ = graphics.CreateTexture(sdr_desc);

  graphics::FramebufferDesc sdr_fb_desc;
  sdr_fb_desc.AddColorAttachment({ .texture = sdr_texture_ });
  sdr_framebuffer_ = graphics.CreateFramebuffer(sdr_fb_desc);
}

} // namespace oxygen::examples::internal
