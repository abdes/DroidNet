//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include "oxygen/platform/types.h"
#include "Oxygen/Renderers/Common/Surface.h"
#include "Oxygen/Renderers/Direct3d12/api_export.h"

namespace oxygen::renderer::d3d12 {

  namespace detail {
    class WindowSurfaceImpl;
  }  // namespace detail

  class WindowSurface : public Surface
  {
    friend OXYGEN_D3D12_API auto CreateWindowSurface(platform::WindowPtr window) -> WindowSurface;
    friend OXYGEN_D3D12_API size_t DestroyWindowSurface(WindowSurface&);
    friend OXYGEN_D3D12_API auto GetSurface(const resources::SurfaceId& surface_id) -> WindowSurface;

  public:
    OXYGEN_D3D12_API ~WindowSurface() override;

    OXYGEN_D3D12_API void SetSize(int width, int height) override;
    OXYGEN_D3D12_API void Present() const override;

    OXYGEN_D3D12_API [[nodiscard]] uint32_t Width() const override;
    OXYGEN_D3D12_API [[nodiscard]] uint32_t Height() const override;
    OXYGEN_D3D12_API [[nodiscard]] ID3D12Resource* BackBuffer() const;
    OXYGEN_D3D12_API [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Rtv() const;
    OXYGEN_D3D12_API [[nodiscard]] D3D12_VIEWPORT Viewport() const;
    OXYGEN_D3D12_API [[nodiscard]] D3D12_RECT Scissor() const;

    OXYGEN_D3D12_API void CreateSwapChain(IDXGIFactory7* factory, ID3D12CommandQueue* command_queue, DXGI_FORMAT format);
    OXYGEN_D3D12_API void Finalize();

  protected:
    OXYGEN_D3D12_API void DoRelease() override;

  private:
    OXYGEN_D3D12_API explicit WindowSurface(const resources::SurfaceId& surface_id, detail::WindowSurfaceImpl* impl);
    WindowSurface(); // Invalid object

    // We do not use a smart pointer here, because the implementation is cleared
    // only once the surface is removed from the resource table.
    detail::WindowSurfaceImpl* impl_{ nullptr };
  };

  OXYGEN_D3D12_API auto CreateWindowSurface(platform::WindowPtr window) -> WindowSurface;
  OXYGEN_D3D12_API auto DestroyWindowSurface(resources::SurfaceId& surface_id) -> size_t;
  OXYGEN_D3D12_API auto GetSurface(const resources::SurfaceId& surface_id) -> WindowSurface;

}  // namespace oxygen::renderer::d3d12
