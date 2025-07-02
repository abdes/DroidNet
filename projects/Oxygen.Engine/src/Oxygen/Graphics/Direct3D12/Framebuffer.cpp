//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::Texture;
using oxygen::graphics::d3d12::Framebuffer;

Framebuffer::Framebuffer(FramebufferDesc desc, RenderController* renderer)
  : desc_(std::move(desc))
  , renderer_(renderer)
{
  DCHECK_NOTNULL_F(renderer_, "RenderController must not be null");

  DCHECK_F(!desc_.color_attachments.empty() || desc_.depth_attachment.IsValid(),
    "Framebuffer must have at least one color or depth attachment");
  DCHECK_F(desc_.color_attachments.size() <= kMaxRenderTargets,
    "Framebuffer can have at most {} color attachments", kMaxRenderTargets);

  // The framebuffer must have a consistent size across all attachments. We
  // will use the size of the first color attachment, or if none is provided,
  // the depth attachment.
  if (!desc_.color_attachments.empty()) {
    const auto texture = desc_.color_attachments[0].texture;
    rt_width_ = texture->GetDescriptor().width;
    rt_height_ = texture->GetDescriptor().height;
  } else if (desc_.depth_attachment.IsValid()) {
    const auto texture = desc_.depth_attachment.texture;
    rt_width_ = texture->GetDescriptor().width;
    rt_height_ = texture->GetDescriptor().height;
  }

  auto& resource_registry = renderer_->GetResourceRegistry();

  for (const auto& attachment : desc_.color_attachments) {
    auto texture = attachment.texture;

    DCHECK_EQ_F(texture->GetDescriptor().width, rt_width_,
      "Framebuffer {}: width mismatch between attachments", texture->GetName());
    DCHECK_EQ_F(texture->GetDescriptor().height, rt_height_,
      "Framebuffer {}: height mismatch between attachments",
      texture->GetName());

    auto rtv_handle = renderer_->GetDescriptorAllocator().Allocate(
      ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly);
    if (!rtv_handle.IsValid()) {
      throw std::runtime_error(fmt::format(
        "Failed to allocate RTV handle for color attachment in texture `{}`",
        texture->GetName()));
    }

    resource_registry.Register(texture);

    TextureViewDescription view_desc {
      .view_type = ResourceViewType::kTexture_RTV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = attachment.format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = attachment.sub_resources,
    };

    auto rtv = resource_registry.RegisterView(
      *texture, std::move(rtv_handle), view_desc);
    if (!rtv.IsValid()) {
      resource_registry.UnRegisterResource(*texture);
      throw std::runtime_error(fmt::format(
        "Failed to register RTV view for texture `{}`", texture->GetName()));
    }
    rtvs_.push_back(rtv.AsInteger());
    textures_.push_back(std::move(texture));
  }

  if (auto& depth_attachment = desc_.depth_attachment;
    depth_attachment.IsValid()) {
    auto texture = depth_attachment.texture;
    DCHECK_EQ_F(texture->GetDescriptor().width, rt_width_,
      "Framebuffer {}: width mismatch between attachments", texture->GetName());
    DCHECK_EQ_F(texture->GetDescriptor().height, rt_height_,
      "Framebuffer {}: height mismatch between attachments",
      texture->GetName());

    DescriptorHandle dsv_handle = renderer_->GetDescriptorAllocator().Allocate(
      ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
    if (!dsv_handle.IsValid()) {
      throw std::runtime_error(fmt::format(
        "Failed to allocate DSV handle for color attachment in texture `{}`",
        texture->GetName()));
    }

    resource_registry.Register(texture);

    TextureViewDescription view_desc {
      .view_type = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = depth_attachment.format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = depth_attachment.sub_resources,
    };

    auto dsv = resource_registry.RegisterView(
      *texture, std::move(dsv_handle), view_desc);
    if (!dsv.IsValid()) {
      resource_registry.UnRegisterResource(*texture);
      throw std::runtime_error(fmt::format(
        "Failed to register DSV view for texture `{}`", texture->GetName()));
    }
    dsv_ = dsv.AsInteger();

    textures_.push_back(std::move(texture));
  }
}

Framebuffer::~Framebuffer()
{
  DCHECK_NOTNULL_F(renderer_, "RenderController must not be null");

  LOG_SCOPE_F(1, "Destroying framebuffer");
  auto& resource_registry = renderer_->GetResourceRegistry();
  for (const auto& texture : textures_) {
    DCHECK_NOTNULL_F(texture, "Texture must not be null");
    DLOG_F(1, "for texture {}", texture->GetName());
    resource_registry.UnRegisterResource(*texture);
  }
  textures_.clear();
  rtvs_.clear();
}

auto Framebuffer::GetFramebufferInfo() const -> const FramebufferInfo&
{
  static FramebufferInfo info(desc_);
  return info;
}
