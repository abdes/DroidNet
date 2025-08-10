//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/FrameBuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>

using oxygen::graphics::CommandRecorder;

using oxygen::graphics::Color;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferAttachment;
using oxygen::graphics::FramebufferInfo;

auto FramebufferAttachment::ResolveClearColor(
  const std::optional<Color>& explicit_clear) const -> Color
{
  if (explicit_clear.has_value()) {
    return explicit_clear.value();
  }
  if (texture && texture->GetDescriptor().use_clear_value) {
    return texture->GetDescriptor().clear_value;
  }
  return { 0.0f, 0.0f, 0.0f, 0.0f };
}

auto FramebufferAttachment::ResolveDepthStencil(
  const std::optional<float>& explicit_depth,
  const std::optional<uint8_t>& explicit_stencil) const
  -> std::pair<float, uint8_t>
{
  using oxygen::graphics::detail::FormatInfo;
  using oxygen::graphics::detail::GetFormatInfo;

  const auto format_info = GetFormatInfo(format);

  float depth = 1.0f;
  uint8_t stencil = 0;
  if (explicit_depth.has_value()) {
    depth = explicit_depth.value();
  } else if (texture && texture->GetDescriptor().use_clear_value
    && format_info.has_depth) {
    depth = texture->GetDescriptor().clear_value.r;
  }
  if (explicit_stencil.has_value()) {
    stencil = explicit_stencil.value();
  } else if (texture && texture->GetDescriptor().use_clear_value
    && format_info.has_stencil) {
    stencil = static_cast<uint8_t>(texture->GetDescriptor().clear_value.g);
  }
  return { depth, stencil };
}

FramebufferInfo::FramebufferInfo(const FramebufferDesc& desc)
{
  for (const auto& attachment : desc.color_attachments) {
    color_formats_.push_back(
      attachment.format == Format::kUnknown && attachment.texture
        ? attachment.texture->GetDescriptor().format
        : attachment.format);
  }

  if (desc.depth_attachment.IsValid()) {
    const TextureDesc& texture_desc
      = desc.depth_attachment.texture->GetDescriptor();
    depth_format_ = texture_desc.format;
    sample_count_ = texture_desc.sample_count;
    sample_quality_ = texture_desc.sample_quality;
  } else if (!desc.color_attachments.empty()
    && desc.color_attachments[0].IsValid()) {
    const TextureDesc& texture_desc
      = desc.color_attachments[0].texture->GetDescriptor();
    sample_count_ = texture_desc.sample_count;
    sample_quality_ = texture_desc.sample_quality;
  }
}

void Framebuffer::PrepareForRender(CommandRecorder& recorder)
{
  const auto& desc = GetDescriptor();
  for (const auto& attachment : desc.color_attachments) {
    if (attachment.texture) {
      recorder.BeginTrackingResourceState(
        *attachment.texture, ResourceStates::kPresent, true);
      recorder.RequireResourceState(
        *attachment.texture, ResourceStates::kRenderTarget);
    }
  }

  if (desc.depth_attachment.IsValid()) {
    // Depth attachment starts in the DepthWrite state
    recorder.BeginTrackingResourceState(
      *desc.depth_attachment.texture, ResourceStates::kDepthWrite, true);
  }

  // Flush barriers to ensure all resource state transitions are applied and
  // that subsequent state transitions triggered by the frame rendering task
  // (application) are executed in a separate batch.
  recorder.FlushBarriers();
}
