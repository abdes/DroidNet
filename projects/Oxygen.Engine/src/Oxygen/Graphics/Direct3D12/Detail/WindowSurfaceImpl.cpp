//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Direct3d12/Detail/WindowSurfaceImpl.h"

#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Graphics/Common/ObjectRelease.h"
#include "Oxygen/Graphics/Direct3D12/Graphics.h"
#include "Oxygen/Graphics/Direct3d12/Renderer.h"

using oxygen::graphics::d3d12::detail::DescriptorHandle;
using oxygen::graphics::d3d12::detail::GetFactory;
using oxygen::graphics::d3d12::detail::GetMainDevice;
using oxygen::graphics::d3d12::detail::GetRenderer;
using oxygen::graphics::d3d12::detail::WindowSurfaceImpl;
using oxygen::windows::ThrowOnFailed;

namespace {

DXGI_FORMAT ToNonSrgb(const DXGI_FORMAT format)
{
    switch (format) { // NOLINT(clang-diagnostic-switch-enum)
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    default:
        return format;
    }
}
} // namespace

void WindowSurfaceImpl::Resize()
{
    DCHECK_NOTNULL_F(swap_chain_);
    const auto [width, height] = window_.lock()->GetFrameBufferSize();
    try {
        for (auto& [resource, rtv] : render_targets_) {
            ObjectRelease(resource);
        }
        ThrowOnFailed(swap_chain_->ResizeBuffers(
            kFrameBufferCount,
            width, height,
            format_, // ToNonSrgb(format_),
            DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to resize swap chain: {}", e.what());
    }
    ShouldResize(false);
    Finalize();
}

void WindowSurfaceImpl::Present() const
{
    DCHECK_NOTNULL_F(swap_chain_);
    ThrowOnFailed(swap_chain_->Present(1, 0));
    current_backbuffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void WindowSurfaceImpl::CreateSwapChain(const DXGI_FORMAT format)
{
    // This method may be called multiple times, therefore we need to ensure that
    // any remaining resources from previous calls are released first.
    if (swap_chain_)
        ReleaseSwapChain();

    // Remember the format used during swap-chain creation, and use it for the
    // render target creation in Finalize()
    format_ = format;

    const auto window = window_.lock();
    CHECK_NOTNULL_F(window, "window is not valid");

    const DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {
        .Width = Width(),
        .Height = Height(),
        .Format = ToNonSrgb(format),
        .Stereo = FALSE,
        .SampleDesc = { 1, 0 }, // Always like this for D3D12
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER, // For now we will use the back buffer as a render target
        .BufferCount = kFrameBufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
    };

    IDXGISwapChain1* swap_chain { nullptr };
    const auto window_handle = static_cast<HWND>(window->NativeWindow().window_handle);
    try {
        ThrowOnFailed(
            GetFactory()->CreateSwapChainForHwnd(
                command_queue_,
                window_handle,
                &swap_chain_desc,
                nullptr,
                nullptr,
                &swap_chain));
        ThrowOnFailed(GetFactory()->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER));
        ThrowOnFailed(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_)));
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to create swap chain: {}", e.what());
        ObjectRelease(swap_chain);
        ObjectRelease(swap_chain_);
    }
    ObjectRelease(swap_chain);

    for (auto& [resource, rtv] : render_targets_) {
        rtv = GetRenderer().RtvHeap().Allocate();
    }

    Finalize();
}

void WindowSurfaceImpl::Finalize()
{
    current_backbuffer_index_ = swap_chain_->GetCurrentBackBufferIndex();

    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        DCHECK_F(render_targets_[i].resource == nullptr);
        ID3D12Resource* back_buffer { nullptr };
        try {
            ThrowOnFailed(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
            render_targets_[i].resource = back_buffer;
            const D3D12_RENDER_TARGET_VIEW_DESC rtv_desc {
                .Format = format_,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D = { 0, 0 }
            };
            GetMainDevice()->CreateRenderTargetView(back_buffer, &rtv_desc, render_targets_[i].rtv.cpu);
        } catch (const std::exception& e) {
            LOG_F(ERROR, "Failed to create render target view: {}", e.what());
            ObjectRelease(back_buffer);
        }
    }

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {};
    ThrowOnFailed(swap_chain_->GetDesc1(&swap_chain_desc));
    const auto [width, height] = window_.lock()->GetFrameBufferSize();
    DCHECK_EQ_F(width, swap_chain_desc.Width);
    DCHECK_EQ_F(height, swap_chain_desc.Height);

    // Set viewport
    viewport_.top_left_x = 0.0f;
    viewport_.top_left_y = 0.0f;
    viewport_.width = static_cast<float>(width);
    viewport_.height = static_cast<float>(height);
    viewport_.max_depth = 0.0f;
    viewport_.max_depth = 1.0f;

    // Set scissor rectangle
    scissor_.left = 0;
    scissor_.top = 0;
    scissor_.right = static_cast<LONG>(width);
    scissor_.bottom = static_cast<LONG>(height);
}

void WindowSurfaceImpl::ReleaseSwapChain()
{
    for (auto& [resource, rtv] : render_targets_) {
        ObjectRelease(resource);
        GetRenderer().RtvHeap().Free(rtv);
    }
    ObjectRelease(swap_chain_);
}

auto WindowSurfaceImpl::Width() const -> uint32_t
{
    return static_cast<uint32_t>(viewport_.width);
}

auto WindowSurfaceImpl::Height() const -> uint32_t
{
    return static_cast<uint32_t>(viewport_.height);
}

auto WindowSurfaceImpl::CurrentBackBuffer() const -> ID3D12Resource*
{
    return render_targets_[current_backbuffer_index_].resource;
}

auto WindowSurfaceImpl::Rtv() const -> const DescriptorHandle&
{
    return render_targets_[current_backbuffer_index_].rtv;
}

auto WindowSurfaceImpl::GetViewPort() const -> ViewPort
{
    return viewport_;
}

auto WindowSurfaceImpl::GetScissors() const -> Scissors
{
    return scissor_;
}
