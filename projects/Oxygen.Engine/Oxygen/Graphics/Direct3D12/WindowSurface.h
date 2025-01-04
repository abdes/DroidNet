//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Graphics/Common/Surface.h"
#include "Oxygen/Graphics/Direct3d12/D3DResource.h"
#include "Oxygen/Graphics/Direct3d12/Forward.h"
#include "Oxygen/Graphics/Direct3d12/api_export.h"
#include "Oxygen/Platform/Common/Types.h"

namespace oxygen::graphics::d3d12 {

namespace detail {
  class WindowSurfaceImpl;
} // namespace detail

class WindowSurface : public graphics::WindowSurface, public D3DResource
{
  using Super = graphics::WindowSurface;

 public:
  OXYGEN_D3D12_API ~WindowSurface() override = default;

  OXYGEN_DEFAULT_COPYABLE(WindowSurface);
  OXYGEN_DEFAULT_MOVABLE(WindowSurface);

  OXYGEN_D3D12_API void Resize(int width, int height) override;
  OXYGEN_D3D12_API void Present() const override;

  [[nodiscard]] auto GetResource() const -> ID3D12Resource* override;

 protected:
  OXYGEN_D3D12_API void InitializeSurface() override;
  OXYGEN_D3D12_API void ReleaseSurface() noexcept override;

 private:
  WindowSurface(const resources::SurfaceId& surface_id, platform::WindowPtr window, detail::WindowSurfaceImplPtr impl);
  friend class Renderer; // Renderer needs to create WindowSurface instances

  detail::WindowSurfaceImplPtr pimpl_ {};
};

} // namespace oxygen::graphics::d3d12
