//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/FramebufferImpl.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>

using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::Texture;
using oxygen::graphics::internal::FramebufferImpl;

FramebufferImpl::FramebufferImpl(
  FramebufferDesc desc, std::weak_ptr<Graphics> gfx_weak)
  : desc_(std::move(desc))
  , gfx_weak_(std::move(gfx_weak))
{
  DCHECK_F(!gfx_weak_.expired(), "Graphics must not be null");
  auto gfx = gfx_weak_.lock();

  DCHECK_F(!desc_.color_attachments.empty() || desc_.depth_attachment.IsValid(),
    "FramebufferImpl must have at least one color or depth attachment");
  DCHECK_F(desc_.color_attachments.size() <= kMaxRenderTargets,
    "FramebufferImpl can have at most {} color attachments", kMaxRenderTargets);

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

  auto& resource_registry = gfx->GetResourceRegistry();

  for (const auto& attachment : desc_.color_attachments) {
    auto texture = attachment.texture;

    DCHECK_EQ_F(texture->GetDescriptor().width, rt_width_,
      "FramebufferImpl {}: width mismatch between attachments",
      texture->GetName());
    DCHECK_EQ_F(texture->GetDescriptor().height, rt_height_,
      "FramebufferImpl {}: height mismatch between attachments",
      texture->GetName());

    auto rtv_handle = gfx->GetDescriptorAllocator().Allocate(
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
    if (!rtv->IsValid()) {
      resource_registry.UnRegisterResource(*texture);
      throw std::runtime_error(fmt::format(
        "Failed to register RTV view for texture `{}`", texture->GetName()));
    }
    rtvs_.push_back(rtv);
    textures_.push_back(std::move(texture));
  }

  if (auto& depth_attachment = desc_.depth_attachment;
    depth_attachment.IsValid()) {
    auto texture = depth_attachment.texture;
    DCHECK_EQ_F(texture->GetDescriptor().width, rt_width_,
      "FramebufferImpl {}: width mismatch between attachments",
      texture->GetName());
    DCHECK_EQ_F(texture->GetDescriptor().height, rt_height_,
      "FramebufferImpl {}: height mismatch between attachments",
      texture->GetName());

    DescriptorHandle dsv_handle = gfx->GetDescriptorAllocator().Allocate(
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
    if (!dsv->IsValid()) {
      resource_registry.UnRegisterResource(*texture);
      throw std::runtime_error(fmt::format(
        "Failed to register DSV view for texture `{}`", texture->GetName()));
    }
    dsv_ = dsv;

    textures_.push_back(std::move(texture));
  }
}

FramebufferImpl::~FramebufferImpl()
{
  if (gfx_weak_.expired()) {
    DLOG_F(2, "Graphics object is no longer valid");
    return;
  }

  auto gfx = gfx_weak_.lock();

  LOG_SCOPE_F(1, "Destroying framebuffer");
  auto& resource_registry = gfx->GetResourceRegistry();
  for (const auto& texture : textures_) {
    DCHECK_NOTNULL_F(texture, "Texture must not be null");
    DLOG_F(1, "for texture {}", texture->GetName());
    resource_registry.UnRegisterResource(*texture);
  }
  textures_.clear();
  rtvs_.clear();
}

auto FramebufferImpl::GetFramebufferInfo() const -> const FramebufferInfo&
{
  static FramebufferInfo info(desc_);
  return info;
}

auto FramebufferImpl::PrepareForRender(CommandRecorder& recorder) -> void
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
