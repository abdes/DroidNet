//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Platform/Types.h"
#include "Oxygen/Renderers/Common/Surface.h"
#include "Oxygen/Renderers/Direct3d12/api_export.h"
#include "Oxygen/Renderers/Direct3d12/D3DResource.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12 {
  constexpr static DXGI_FORMAT kDefaultBackBufferFormat{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };

  namespace detail {
    class WindowSurfaceImpl;
  }  // namespace detail

  class WindowSurface : public renderer::WindowSurface, public D3DResource
  {
    using Super = renderer::WindowSurface;

  public:
    OXYGEN_D3D12_API ~WindowSurface() override = default;

    OXYGEN_DEFAULT_COPYABLE(WindowSurface);
    OXYGEN_DEFAULT_MOVABLE(WindowSurface);

    OXYGEN_D3D12_API void Resize(int width, int height) override;
    OXYGEN_D3D12_API void Present() const override;

    OXYGEN_D3D12_API [[nodiscard]] D3D12_VIEWPORT Viewport() const;
    OXYGEN_D3D12_API [[nodiscard]] D3D12_RECT Scissor() const;

    [[nodiscard]] auto GetResource() const->ID3D12Resource* override;

  protected:
    OXYGEN_D3D12_API void InitializeSurface() override;
    OXYGEN_D3D12_API void ReleaseSurface() noexcept override;

  private:
    WindowSurface(const resources::SurfaceId& surface_id, platform::WindowPtr window, detail::WindowSurfaceImplPtr impl);
    friend class Renderer; // Renderer needs to create WindowSurface instances

    detail::WindowSurfaceImplPtr pimpl_{ };
  };

}  // namespace oxygen::renderer::d3d12
