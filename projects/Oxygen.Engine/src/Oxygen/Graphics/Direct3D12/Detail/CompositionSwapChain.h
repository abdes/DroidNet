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
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

namespace oxygen::graphics::d3d12 {

namespace detail {
  class CompositionSurface;

  class CompositionSwapChain : public Component {
    OXYGEN_COMPONENT(CompositionSwapChain)
  public:
    CompositionSwapChain(dx::ICommandQueue* command_queue, const DXGI_FORMAT format,
      const Graphics* graphics);

    ~CompositionSwapChain() noexcept override;

    OXYGEN_MAKE_NON_COPYABLE(CompositionSwapChain)
    OXYGEN_DEFAULT_MOVABLE(CompositionSwapChain)

    [[nodiscard]] auto IsValid() const { return swap_chain_ != nullptr; }

    [[nodiscard]] auto GetSwapChain() const { return swap_chain_; }

    // Present the current frame to the screen.
    auto Present() const -> void;

    [[nodiscard]] auto GetFormat() const { return format_; }
    auto SetFormat(const DXGI_FORMAT format) -> void { format_ = format; }

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
        "back buffer index {} is out of range ({})", index,
        render_targets_.size());
      return render_targets_[index];
    }

    auto Resize(uint32_t width, uint32_t height) -> void;

  private:
    friend class CompositionSurface;
    auto CreateSwapChain() -> void;
    auto CreateRenderTargets() -> void;
    auto ReleaseRenderTargets() -> void;
    auto ReleaseSwapChain() -> void;

    DXGI_FORMAT format_ { kDefaultBackBufferFormat };
    dx::ICommandQueue* command_queue_;
    const Graphics* graphics_;

    IDXGISwapChain4* swap_chain_ { nullptr };

    mutable uint32_t current_back_buffer_index_ { 0 };
    StaticVector<std::shared_ptr<Texture>, frame::kFramesInFlight.get()>
      render_targets_ {};
  };

} // namespace detail
} // namespace oxygen::graphics::d3d12
