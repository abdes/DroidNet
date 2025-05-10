//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgi1_5.h>
#include <dxgiformat.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>

namespace oxygen::graphics::d3d12::detail {

class WindowSurface;

// TODO: pass the Graphics Backend instance to the SwapChain constructor
class SwapChain : public Component {
    OXYGEN_COMPONENT(SwapChain)
    OXYGEN_COMPONENT_REQUIRES(oxygen::graphics::detail::WindowComponent)
public:
    SwapChain(dx::ICommandQueue* command_queue, DXGI_FORMAT format)
        : command_queue_(command_queue)
        , format_(format)
    {
    }

    ~SwapChain() noexcept override;

    OXYGEN_MAKE_NON_COPYABLE(SwapChain);
    OXYGEN_DEFAULT_MOVABLE(SwapChain);

    [[nodiscard]] auto IsValid() const { return swap_chain_ != nullptr; }

    // Present the current frame to the screen.
    void Present() const;

    [[nodiscard]] virtual auto GetResource() const -> ID3D12Resource*
    {
        return render_targets_[current_back_buffer_index_].resource;
    }

    [[nodiscard]] auto GetFormat() const { return format_; }
    void SetFormat(DXGI_FORMAT format) { format_ = format; }

    [[nodiscard]] auto GetCurrentRenderTargetView() const -> const DescriptorHandle&
    {
        return render_targets_[current_back_buffer_index_].rtv;
    }

protected:
    void UpdateDependencies(const Composition& composition) override;

private:
    friend class WindowSurface;
    void CreateSwapChain();
    void Finalize();
    void ReleaseSwapChain();
    void Resize();

    DXGI_FORMAT format_ { kDefaultBackBufferFormat };
    dx::ICommandQueue* command_queue_;

    IDXGISwapChain4* swap_chain_ { nullptr };

    mutable uint32_t current_back_buffer_index_ { 0 };
    struct {
        ID3D12Resource* resource { nullptr };
        DescriptorHandle rtv {};
    } render_targets_[kFrameBufferCount] {};

    oxygen::graphics::detail::WindowComponent* window_ {};
};

} // namespace oxygen::graphics::d3d12::detail
