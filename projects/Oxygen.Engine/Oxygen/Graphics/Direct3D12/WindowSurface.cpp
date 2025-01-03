//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Direct3d12/WindowSurface.h"


#include "Detail/WindowSurfaceImpl.h"

using oxygen::renderer::resources::SurfaceId;
using oxygen::platform::WindowPtr;
using oxygen::renderer::Surface;
using oxygen::renderer::d3d12::WindowSurface;


WindowSurface::WindowSurface(
  const SurfaceId& surface_id,
  WindowPtr window,
  std::shared_ptr<detail::WindowSurfaceImpl> impl)
  : renderer::WindowSurface(surface_id, std::move(window))
  , pimpl_(std::move(impl))
{
}

void WindowSurface::Resize(const int /*width*/, const int /*height*/)
{
  pimpl_->ShouldResize(true);
}

void WindowSurface::Present() const
{
  pimpl_->Present();
}

auto WindowSurface::GetResource() const -> ID3D12Resource*
{
  return pimpl_->GetResource();
}

void WindowSurface::InitializeSurface()
{
  Super::InitializeSurface();
  pimpl_->CreateSwapChain();
}

void WindowSurface::ReleaseSurface() noexcept
{
  Super::ReleaseSurface();
  pimpl_->ReleaseSwapChain();
}
