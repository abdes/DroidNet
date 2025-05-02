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
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>

namespace oxygen::graphics::d3d12::detail {

// TODO: pass the Graphics Backend instance to the SwapChain constructor
class SwapChain : public Component {
    OXYGEN_COMPONENT(SwapChain)
    OXYGEN_COMPONENT_REQUIRES(oxygen::graphics::detail::WindowComponent)
public:
    SwapChain(CommandQueueType* command_queue, DXGI_FORMAT format)
        : command_queue_(command_queue)
        , format_(format)
    {
    }

    ~SwapChain() noexcept override;

    OXYGEN_MAKE_NON_COPYABLE(SwapChain);
    OXYGEN_DEFAULT_MOVABLE(SwapChain);

    [[nodiscard]] auto IsValid() const { return swap_chain_ != nullptr; }

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

    [[nodiscard]] auto GetViewPort() const -> const ViewPort&
    {
        return viewport_;
    }

    [[nodiscard]] auto GetScissors() const -> const Scissors&
    {
        return scissor_;
    }

    void ShouldResize(const bool flag) { should_resize_ = flag; }
    auto ShouldResize() const -> bool { return should_resize_; }

protected:
    void UpdateDependencies(const Composition& composition) override
    {
        window_ = &(composition.GetComponent<oxygen::graphics::detail::WindowComponent>());
        CreateSwapChain();
    }

private:
    friend WindowSurface;
    void CreateSwapChain();
    void Finalize();
    void ReleaseSwapChain();
    void Resize();

    DXGI_FORMAT format_ { kDefaultBackBufferFormat };
    CommandQueueType* command_queue_;

    IDXGISwapChain4* swap_chain_ { nullptr };
    bool should_resize_ { false };

    mutable uint32_t current_back_buffer_index_ { 0 };
    struct {
        ID3D12Resource* resource { nullptr };
        DescriptorHandle rtv {};
    } render_targets_[kFrameBufferCount] {};
    ViewPort viewport_ {};
    Scissors scissor_ {};

    oxygen::graphics::detail::WindowComponent* window_ {};
};

} // namespace oxygen::graphics::d3d12::detail
