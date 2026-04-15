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

    const bool owns_registration = resource_registry.AcquireRegistration(texture);

    TextureViewDescription view_desc {
      .view_type = ResourceViewType::kTexture_RTV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = attachment.format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = attachment.sub_resources,
    };

    auto rtv = resource_registry.Find(*texture, view_desc);
    bool owns_view_registration = false;
    if (!rtv->IsValid()) {
      auto rtv_handle = gfx->GetDescriptorAllocator().AllocateRaw(
        ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly);
      if (!rtv_handle.IsValid()) {
        throw std::runtime_error(fmt::format(
          "Failed to allocate RTV handle for color attachment in texture `{}`",
          texture->GetName()));
      }
      auto acquired = resource_registry.AcquireViewRegistration(
        *texture, std::move(rtv_handle), view_desc);
      rtv = acquired.first;
      owns_view_registration = acquired.second;
    }
    if (!rtv->IsValid()) {
      if (owns_registration) {
        resource_registry.UnRegisterResource(*texture);
      }
      throw std::runtime_error(fmt::format(
        "Failed to register RTV view for texture `{}`", texture->GetName()));
    }
    rtvs_.push_back(rtv);
    textures_.push_back(std::move(texture));
    owns_resource_registration_.push_back(owns_registration);
    owns_view_registration_.push_back(owns_view_registration);
    registered_views_.push_back(rtv);
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

    const bool owns_registration = resource_registry.AcquireRegistration(texture);

    TextureViewDescription view_desc {
      .view_type = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = depth_attachment.format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = depth_attachment.sub_resources,
      .is_read_only_dsv = depth_attachment.is_read_only,
    };

    auto dsv = resource_registry.Find(*texture, view_desc);
    bool owns_view_registration = false;
    if (!dsv->IsValid()) {
      auto dsv_handle = gfx->GetDescriptorAllocator().AllocateRaw(
        ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
      if (!dsv_handle.IsValid()) {
        throw std::runtime_error(fmt::format(
          "Failed to allocate DSV handle for color attachment in texture `{}`",
          texture->GetName()));
      }
      auto acquired = resource_registry.AcquireViewRegistration(
        *texture, std::move(dsv_handle), view_desc);
      dsv = acquired.first;
      owns_view_registration = acquired.second;
    }
    if (!dsv->IsValid()) {
      if (owns_registration) {
        resource_registry.UnRegisterResource(*texture);
      }
      throw std::runtime_error(fmt::format(
        "Failed to register DSV view for texture `{}`", texture->GetName()));
    }
    dsv_ = dsv;

    textures_.push_back(std::move(texture));
    owns_resource_registration_.push_back(owns_registration);
    owns_view_registration_.push_back(owns_view_registration);
    registered_views_.push_back(dsv);
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
  for (std::size_t index = 0; index < textures_.size(); ++index) {
    const auto& texture = textures_[index];
    DCHECK_NOTNULL_F(texture, "Texture must not be null");
    DLOG_F(1, "for texture {}", texture->GetName());
    if (index < owns_resource_registration_.size()
      && owns_resource_registration_[index]) {
      gfx->ForgetKnownResourceState(*texture);
      resource_registry.UnRegisterResource(*texture);
    } else if (index < owns_view_registration_.size()
      && owns_view_registration_[index]
      && index < registered_views_.size()
      && registered_views_[index]->IsValid()
      && resource_registry.Contains(*texture)) {
      resource_registry.UnRegisterView(*texture, registered_views_[index]);
    }
  }
  textures_.clear();
  owns_resource_registration_.clear();
  owns_view_registration_.clear();
  registered_views_.clear();
  rtvs_.clear();
}

auto FramebufferImpl::GetFramebufferInfo() const -> const FramebufferInfo&
{
  static FramebufferInfo info(desc_);
  return info;
}
