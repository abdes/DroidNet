//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/RenderTarget.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::graphics::d3d12::detail {

//! Represents a surface that is associated with a window.
/*!
 A `WindowSurface` has a swapchain, which size typically corresponds to the
 entire surface of the window. The swapchain is used to present the rendered
 image to the display. Its lifetime is strictly tied to the window lifetime.

 The swapchain is created during the initialization of the window surface and is
 destroyed when the window surface is released. Additionally, the window surface
 listens to window events, such as resizing, minimizing, etc. and triggers a
 resize for the swapchain when needed.
*/

class WindowSurface
    : public graphics::detail::WindowSurface,
      public RenderTarget {

public:
    WindowSurface(platform::WindowPtr window, CommandQueueType* command_queue, DXGI_FORMAT format)
        : graphics::detail::WindowSurface(std::move(window))
    {
        AddComponent<SwapChain>(command_queue, format);
    }

    WindowSurface(platform::WindowPtr window, CommandQueueType* command_queue)
        : graphics::detail::WindowSurface(std::move(window))
    {
        AddComponent<SwapChain>(command_queue, kDefaultBackBufferFormat);
    }

    ~WindowSurface() override = default;

    OXYGEN_MAKE_NON_COPYABLE(WindowSurface);
    OXYGEN_DEFAULT_MOVABLE(WindowSurface);

    void Present() const override
    {
        GetComponent<SwapChain>().Present();
    }

    [[nodiscard]] auto ShouldResize() const -> bool
    {
        return GetComponent<SwapChain>().ShouldResize();
    }

    void Resize() override
    {
        // Resizing the surface will resize the swap chain, which can only
        // happen when the swap chain is not in use. Therefore, we just set the
        // ShouldResize flag and wait for the renderer to call Resize() when
        // it's safe.
        auto& swap_chain = GetComponent<SwapChain>();
        if (!swap_chain.ShouldResize()) {
            swap_chain.ShouldResize(true);
            return;
        }
        swap_chain.Resize();
    }

    [[nodiscard]] auto Width() const -> uint32_t override
    {
        return static_cast<uint32_t>(GetViewPort().width);
    }

    [[nodiscard]] auto Height() const -> uint32_t override
    {
        return static_cast<uint32_t>(GetViewPort().height);
    }

    [[nodiscard]] auto GetViewPort() const -> const ViewPort& override
    {
        return GetComponent<SwapChain>().GetViewPort();
    }

    [[nodiscard]] auto GetScissors() const -> const Scissors& override
    {
        return GetComponent<SwapChain>().GetScissors();
    }

    [[nodiscard]] auto GetResource() const -> ID3D12Resource* override
    {
        return GetComponent<SwapChain>().GetResource();
    }

    [[nodiscard]] auto Rtv() const -> const DescriptorHandle& override
    {
        return GetComponent<SwapChain>().GetCurrentRenderTargetView();
    }
};

} // namespace oxygen::graphics::d3d12::detail
