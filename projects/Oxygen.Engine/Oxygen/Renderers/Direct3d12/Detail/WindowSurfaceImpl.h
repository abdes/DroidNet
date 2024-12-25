// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Surface.h"

#include <shared_mutex>

#include "dx12_utils.h"
#include "oxygen/base/logging.h"
#include "oxygen/base/ResourceTable.h"
#include "Oxygen/Base/Windows/ComError.h"
#include "oxygen/platform/window.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"
#include "resources.h"

using oxygen::renderer::resources::SurfaceId;
using oxygen::platform::WindowPtr;
using oxygen::renderer::Surface;
using oxygen::renderer::d3d12::WindowSurface;
using oxygen::windows::ThrowOnFailed;


#pragma once

namespace oxygen::renderer::d3d12::detail {

  class WindowSurfaceImpl
  {
  public:

    explicit WindowSurfaceImpl(WindowPtr window, CommandQueueType* command_queue)
      : window_(std::move(window)), command_queue_(command_queue)
    {
    }

    void SetSize(int width, int height);
    void Present() const;
    void CreateSwapChain(DXGI_FORMAT format = kDefaultBackBufferFormat);

    [[nodiscard]] uint32_t Width() const;
    [[nodiscard]] uint32_t Height() const;
    [[nodiscard]] ID3D12Resource* BackBuffer() const;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Rtv() const;
    [[nodiscard]] D3D12_VIEWPORT Viewport() const;
    [[nodiscard]] D3D12_RECT Scissor() const;

  private:
    friend class WindowSurface;
    void DoRelease();
    void Finalize();

    std::weak_ptr<platform::Window> window_;
    IDXGISwapChain4* swap_chain_{ nullptr };
    mutable UINT current_backbuffer_index_{ 0 };
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissor_{};
    DXGI_FORMAT format_{ kDefaultBackBufferFormat };  // The format of the swap chain
    CommandQueueType* command_queue_;

    struct RenderTargetData
    {
      ID3D12Resource* resource{ nullptr };
      DescriptorHandle rtv{};
    };
    RenderTargetData render_targets_[kFrameBufferCount]{};
  };

}  // namespace oxygen::renderer::d3d12::detail
