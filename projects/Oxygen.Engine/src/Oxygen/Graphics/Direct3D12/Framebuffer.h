//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>

namespace oxygen::graphics::d3d12 {

class Texture;
class RenderController;

class Framebuffer final : public graphics::Framebuffer {
    using Base = graphics::Framebuffer;

public:
    Framebuffer(FramebufferDesc desc, const RenderController* renderer);
    ~Framebuffer() override;

    OXYGEN_MAKE_NON_COPYABLE(Framebuffer)
    OXYGEN_DEFAULT_MOVABLE(Framebuffer)

    [[nodiscard]] auto GetDescriptor() const -> const FramebufferDesc& override { return desc_; }
    [[nodiscard]] auto GetFramebufferInfo() const -> const FramebufferInfo& override;

    [[nodiscard]] auto GetRenderTargetViews() const
    {
        return std::span(rtvs_.data(), rtvs_.size());
    }

    [[nodiscard]] auto GetDepthStencilView() const -> SIZE_T
    {
        return dsv_;
    }

private:
    FramebufferDesc desc_;
    const RenderController* renderer_;

    StaticVector<std::shared_ptr<graphics::Texture>, kMaxRenderTargets> textures_ {};
    StaticVector<SIZE_T, kMaxRenderTargets> rtvs_ {};
    SIZE_T dsv_ {};

    uint32_t rt_width_ { 0 };
    uint32_t rt_height_ { 0 };
};

}
