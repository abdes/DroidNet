//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <dxgiformat.h>
#include <windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeaps.h>

namespace {

auto ToNonSrgb(const DXGI_FORMAT format) -> DXGI_FORMAT
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

using oxygen::windows::ThrowOnFailed;

using oxygen::graphics::d3d12::NameObject;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::graphics::d3d12::detail::SwapChain;

SwapChain::~SwapChain() noexcept
{
    DLOG_F(INFO, "Release swapchain");
    ReleaseSwapChain();
}

void SwapChain::Present() const
{
    DCHECK_NOTNULL_F(swap_chain_);
    ThrowOnFailed(swap_chain_->Present(1, 0));
    current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void SwapChain::CreateSwapChain()
{
    // This method may be called multiple times, therefore we need to ensure
    // that any remaining resources from previous calls are released first.
    if (swap_chain_ != nullptr) {
        ReleaseSwapChain();
    }

    const DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {
        .Width = window_->Width(),
        .Height = window_->Height(),
        .Format = ToNonSrgb(format_),
        .Stereo = FALSE,
        .SampleDesc = { 1, 0 }, // Always like this for D3D12
        // TODO(abdes): For now, we will use the back buffer as a render target, maybe later render to buffer and copy
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER,
        .BufferCount = kFrameBufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
    };

    IDXGISwapChain1* swap_chain { nullptr };
    auto* const window_handle = static_cast<HWND>(window_->Native().window_handle);
    try {
        // NB: Misleading argument name for CreateSwapChainForHwnd().
        // For Direct3D 11, and earlier versions of Direct3D, the first argument
        // is a pointer to the Direct3D device for the swap chain. For Direct3D
        // 12 this is a pointer to a direct command queue (refer to
        // ID3D12CommandQueue). This parameter cannot be NULL.
        ThrowOnFailed(
            GetGraphics().GetFactory()->CreateSwapChainForHwnd(
                command_queue_, // Yes, the command queue, for D3D12
                window_handle,
                &swap_chain_desc,
                nullptr,
                nullptr,
                &swap_chain));
        ThrowOnFailed(GetGraphics().GetFactory()->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER));
        ThrowOnFailed(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_)));
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to create swap chain: {}", e.what());
        ObjectRelease(swap_chain);
        ObjectRelease(swap_chain_);
    }
    ObjectRelease(swap_chain);

    for (auto& [resource, rtv] : render_targets_) {
        rtv = GetGraphics().Descriptors().RtvHeap().Allocate();
    }

    Finalize();
}

void SwapChain::ReleaseSwapChain()
{
    for (auto& [resource, rtv] : render_targets_) {
        ObjectRelease(resource);
        GetGraphics().Descriptors().RtvHeap().Free(rtv);
    }
    ObjectRelease(swap_chain_);
}

void SwapChain::Finalize()
{
    current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();

    // NOLINTBEGIN(*-pro-bounds-constant-array-index)
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        DCHECK_F(render_targets_[i].resource == nullptr);
        ID3D12Resource* back_buffer { nullptr };
        try {
            ThrowOnFailed(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
            NameObject(back_buffer, "BackBuffer");
            render_targets_[i].resource = back_buffer;
            const D3D12_RENDER_TARGET_VIEW_DESC rtv_desc {
                .Format = format_,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D = { 0, 0 }
            };
            GetGraphics().GetCurrentDevice()->CreateRenderTargetView(back_buffer, &rtv_desc, render_targets_[i].rtv.cpu);
        } catch (const std::exception& e) {
            LOG_F(ERROR, "Failed to create render target view: {}", e.what());
            ObjectRelease(back_buffer);
        }
    }
    // NOLINTEND(*-pro-bounds-constant-array-index)

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {};
    ThrowOnFailed(swap_chain_->GetDesc1(&swap_chain_desc));
    const auto [width, height] = window_->FrameBufferSize();
    DCHECK_EQ_F(width, swap_chain_desc.Width);
    DCHECK_EQ_F(height, swap_chain_desc.Height);

    // Set viewport
    viewport_.top_left_x = 0.0F;
    viewport_.top_left_y = 0.0F;
    viewport_.width = static_cast<float>(width);
    viewport_.height = static_cast<float>(height);
    viewport_.max_depth = 0.0F;
    viewport_.max_depth = 1.0F;

    // Set scissor rectangle
    scissor_.left = 0;
    scissor_.top = 0;
    scissor_.right = static_cast<LONG>(width);
    scissor_.bottom = static_cast<LONG>(height);
}

void SwapChain::Resize()
{
    const auto [width, height] = window_->FrameBufferSize();
    try {
        for (auto& [resource, rtv] : render_targets_) {
            ObjectRelease(resource);
        }
        windows::ThrowOnFailed(swap_chain_->ResizeBuffers(
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
