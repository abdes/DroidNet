//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>

namespace oxygen::graphics::d3d12 {

class RenderTarget : public graphics::RenderTarget {
    using Base = graphics::RenderTarget;

public:
    RenderTarget(detail::SwapChain* swap_chain)
        : swap_chain_(swap_chain)
    {
    }

    ~RenderTarget() noexcept override = default;

    OXYGEN_MAKE_NON_COPYABLE(RenderTarget);
    OXYGEN_DEFAULT_MOVABLE(RenderTarget);

    [[nodiscard]] auto GetViewPort() const -> const ViewPort& override
    {
        return swap_chain_->GetViewPort();
    }

    [[nodiscard]] auto GetScissors() const -> const Scissors& override
    {
        return swap_chain_->GetScissors();
    }

    [[nodiscard]] auto GetResource() const -> ID3D12Resource*
    {
        return swap_chain_->GetResource();
    }

    [[nodiscard]] auto Rtv() const -> const detail::DescriptorHandle&
    {
        return swap_chain_->GetCurrentRenderTargetView();
    }

private:
    detail::SwapChain* swap_chain_ { nullptr };
};

} // namespace oxygen::graphics::d3d12
