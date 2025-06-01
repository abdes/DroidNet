//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::graphics::d3d12::detail {

//! Represents a surface associated with a window.
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
    : public graphics::detail::WindowSurface {

public:
    WindowSurface(platform::WindowPtr window, dx::ICommandQueue* command_queue, DXGI_FORMAT format)
        : graphics::detail::WindowSurface(std::move(window))
    {
        AddComponent<SwapChain>(command_queue, format);
    }

    WindowSurface(platform::WindowPtr window, dx::ICommandQueue* command_queue)
        : graphics::detail::WindowSurface(std::move(window))
    {
        AddComponent<SwapChain>(command_queue, kDefaultBackBufferFormat);
    }

    ~WindowSurface() override = default;

    OXYGEN_MAKE_NON_COPYABLE(WindowSurface);
    OXYGEN_DEFAULT_MOVABLE(WindowSurface);

    void AttachRenderer(const std::shared_ptr<graphics::RenderController> renderer) override
    {
        GetComponent<SwapChain>().AttachRenderer(renderer);
    }

    void DetachRenderer() override
    {
        GetComponent<SwapChain>().DetachRenderer();
    }

    auto GetCurrentBackBufferIndex() const -> uint32_t override
    {
        return GetComponent<SwapChain>().GetCurrentBackBufferIndex();
    }

    auto GetCurrentBackBuffer() const -> std::shared_ptr<graphics::Texture> override
    {
        return GetComponent<SwapChain>().GetCurrentBackBuffer();
    }
    auto GetBackBuffer(uint32_t index) const -> std::shared_ptr<graphics::Texture> override
    {
        return GetComponent<SwapChain>().GetBackBuffer(index);
    }

    void Present() const override
    {
        GetComponent<SwapChain>().Present();
    }

    void Resize() override
    {
        GetComponent<SwapChain>().Resize();
        ShouldResize(false);
    }
};

} // namespace oxygen::graphics::d3d12::detail
