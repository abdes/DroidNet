//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/Constants.h"
#include "Oxygen/Graphics/Direct3D12/Buffer.h"
#include "Oxygen/Graphics/Direct3D12/Constants.h"

#include "Oxygen/Graphics/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Graphics/Direct3d12/RenderTarget.h"
#include "Oxygen/Platform/Window.h"

namespace oxygen::graphics::d3d12::detail {

class WindowSurfaceImpl : public D3DResource, public RenderTarget {
public:
    explicit WindowSurfaceImpl(platform::WindowPtr window, CommandQueueType* command_queue)
        : window_(std::move(window))
        , command_queue_(command_queue)
    {
        mode_ = ResourceAccessMode::kGpuOnly;
    }

    void Present() const;

    void ShouldResize(const bool flag) { should_resize_ = flag; }
    auto ShouldResize() const -> bool { return should_resize_; }
    void Resize();

    [[nodiscard]] auto Width() const -> uint32_t;
    [[nodiscard]] auto Height() const -> uint32_t;
    [[nodiscard]] auto CurrentBackBuffer() const -> ID3D12Resource*;
    [[nodiscard]] auto Rtv() const -> const DescriptorHandle& override;
    [[nodiscard]] auto GetViewPort() const -> ViewPort override;
    [[nodiscard]] auto GetScissors() const -> Scissors override;

    auto GetResource() const -> ID3D12Resource* override { return CurrentBackBuffer(); }

private:
    friend class WindowSurface;
    void CreateSwapChain(DXGI_FORMAT format = kDefaultBackBufferFormat);
    void ReleaseSwapChain();
    void Finalize();

    std::weak_ptr<platform::Window> window_;
    IDXGISwapChain4* swap_chain_ { nullptr };
    bool should_resize_ { false };

    mutable UINT current_backbuffer_index_ { 0 };
    ViewPort viewport_ {};
    Scissors scissor_ {};
    DXGI_FORMAT format_ { kDefaultBackBufferFormat }; // The format of the swap chain
    CommandQueueType* command_queue_;

    struct RenderTargetData {
        ID3D12Resource* resource { nullptr };
        DescriptorHandle rtv {};
    };
    RenderTargetData render_targets_[kFrameBufferCount] {};
};

} // namespace oxygen::graphics::d3d12::detail
