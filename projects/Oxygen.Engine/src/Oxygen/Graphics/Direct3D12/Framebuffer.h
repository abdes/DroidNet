//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

namespace oxygen::graphics::d3d12 {

class Framebuffer final : public graphics::Framebuffer {
    using Base = graphics::Framebuffer;

public:
    explicit Framebuffer(FramebufferDesc desc);
    ~Framebuffer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Framebuffer)
    OXYGEN_DEFAULT_MOVABLE(Framebuffer)

    [[nodiscard]] auto GetDescriptor() const -> const FramebufferDesc& override { return desc_; }
    [[nodiscard]] auto GetFramebufferInfo() const -> const FramebufferInfo& override;

    [[nodiscard]] auto GetRenderTargetViews() const -> std::span<const detail::DescriptorHandle>
    {
        return rtvs_;
    }

    [[nodiscard]] auto GetDepthStencilView() const -> const detail::DescriptorHandle&
    {
        return dsv_;
    }

private:
    FramebufferDesc desc_;

    StaticVector<std::shared_ptr<Texture>, kMaxRenderTargets + 1> textures_;
    StaticVector<detail::DescriptorHandle, kMaxRenderTargets> rtvs_;
    detail::DescriptorHandle dsv_;

    uint32_t rt_width { 0 };
    uint32_t rt_height { 0 };
};

} // namespace oxygen::graphics::d3d12
