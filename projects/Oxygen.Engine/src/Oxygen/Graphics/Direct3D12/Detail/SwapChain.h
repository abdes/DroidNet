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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

namespace oxygen::graphics::d3d12 {

class RenderController;

namespace detail {
    class WindowSurface;

    // TODO: pass the Graphics Backend instance to the SwapChain constructor
    class SwapChain : public Component {
        OXYGEN_COMPONENT(SwapChain)
        OXYGEN_COMPONENT_REQUIRES(oxygen::graphics::detail::WindowComponent)
    public:
        SwapChain(dx::ICommandQueue* command_queue, const DXGI_FORMAT format)
            : format_(format)
            , command_queue_(command_queue)
        {
        }

        ~SwapChain() noexcept override;

        OXYGEN_MAKE_NON_COPYABLE(SwapChain)
        OXYGEN_DEFAULT_MOVABLE(SwapChain)

        [[nodiscard]] auto IsValid() const { return swap_chain_ != nullptr; }

        void AttachRenderer(std::shared_ptr<graphics::RenderController> renderer);
        void DetachRenderer();

        // Present the current frame to the screen.
        void Present() const;

        [[nodiscard]] auto GetFormat() const { return format_; }
        void SetFormat(const DXGI_FORMAT format) { format_ = format; }

        auto GetCurrentBackBufferIndex() const -> uint32_t
        {
            DCHECK_NOTNULL_F(swap_chain_);
            return swap_chain_->GetCurrentBackBufferIndex();
        }

        auto GetCurrentBackBuffer() const -> std::shared_ptr<Texture>
        {
            return render_targets_[current_back_buffer_index_];
        }

        auto GetBackBuffer(uint32_t index) const -> std::shared_ptr<Texture>
        {
            CHECK_F(index < render_targets_.size(),
                "back buffer index {} is out of range ({})", index, render_targets_.size());
            return render_targets_[index];
        }

    protected:
        void UpdateDependencies(const Composition& composition) override;

    private:
        friend class WindowSurface;
        void CreateSwapChain();
        void CreateRenderTargets();
        void ReleaseRenderTargets();
        void Resize();
        void ReleaseSwapChain();

        DXGI_FORMAT format_ { kDefaultBackBufferFormat };
        dx::ICommandQueue* command_queue_;

        IDXGISwapChain4* swap_chain_ { nullptr };

        mutable uint32_t current_back_buffer_index_ { 0 };
        StaticVector<std::shared_ptr<Texture>, kFrameBufferCount> render_targets_ {};

        graphics::detail::WindowComponent* window_ { nullptr };
        std::shared_ptr<RenderController> renderer_;
    };

} // namespace detail

} // namespace oxygen::graphics::d3d12
