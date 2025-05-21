//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeaps.h>

using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::d3d12::Framebuffer;
using oxygen::graphics::d3d12::detail::DescriptorHandle;
using oxygen::graphics::d3d12::detail::GetGraphics;

Framebuffer::Framebuffer(FramebufferDesc desc)
    : desc_(std::move(desc))
{
    // The framebuffer must have a consistent size across all attachments. We
    // will use the size of the first color attachment, or if none is provided,
    // the depth attachment.
    if (!desc_.color_attachments.empty()) {
        const auto texture = static_pointer_cast<Texture>(desc_.color_attachments[0].texture);
        rt_width = texture->GetDescriptor().width;
        rt_height = texture->GetDescriptor().height;
    } else if (desc_.depth_attachment.IsValid()) {
        const auto texture = static_pointer_cast<Texture>(desc_.depth_attachment.texture);
        rt_width = texture->GetDescriptor().width;
        rt_height = texture->GetDescriptor().height;
    }

    for (const auto& attachment : desc_.color_attachments) {
        auto texture = static_pointer_cast<Texture>(desc_.color_attachments[0].texture);
        assert(texture->GetDescriptor().width == rt_width);
        assert(texture->GetDescriptor().height == rt_height);

        DescriptorHandle rtv = GetGraphics().Descriptors().RtvHeap().Allocate();
        texture->CreateRenderTargetView(rtv, attachment.format, attachment.sub_resources);

        rtvs_.push_back(std::move(rtv));
        textures_.push_back(std::move(texture));
    }

    if (desc_.depth_attachment.IsValid()) {
        auto texture = static_pointer_cast<Texture>(desc_.depth_attachment.texture);
        assert(texture->GetDescriptor().width == rt_width);
        assert(texture->GetDescriptor().height == rt_height);

        dsv_ = GetGraphics().Descriptors().DsvHeap().Allocate();
        texture->CreateDepthStencilView(
            dsv_,
            desc_.depth_attachment.format,
            desc_.depth_attachment.sub_resources,
            desc_.depth_attachment.is_read_only);

        textures_.push_back(std::move(texture));
    }
}

auto Framebuffer::GetFramebufferInfo() const -> const FramebufferInfo&
{
    static FramebufferInfo info(desc_);
    return info;
}
