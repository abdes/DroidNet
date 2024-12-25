//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Surface.h"

#include <shared_mutex>

#include "detail/dx12_utils.h"
#include "Detail/WindowSurfaceImpl.h"
#include "oxygen/base/logging.h"
#include "oxygen/base/ResourceTable.h"
#include "Oxygen/Base/Windows/ComError.h"
#include "oxygen/platform/window.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"

using oxygen::renderer::resources::SurfaceId;
using oxygen::platform::WindowPtr;
using oxygen::renderer::Surface;
using oxygen::renderer::d3d12::WindowSurface;
using oxygen::windows::ThrowOnFailed;


WindowSurface::WindowSurface(const SurfaceId& surface_id, WindowPtr window, std::shared_ptr<detail::WindowSurfaceImpl> impl)
  : oxygen::renderer::WindowSurface(surface_id, std::move(window)), pimpl_(std::move(impl))
{
}


//#define CALL_IMPL(statement, ...) \
//  if (!surfaces.Contains(GetId())) { \
//    LOG_F(WARNING, "WindowSurface resource with Id `{}` does not exist anymore", GetId().ToString()); \
//    return; \
//  } \
//  pimpl_->statement
//
//#define RETURN_IMPL(statement, error_value) \
//  if (!surfaces.Contains(GetId())) { \
//    LOG_F(WARNING, "WindowSurface resource with Id `{}` does not exist anymore", GetId().ToString()); \
//    return error_value; \
//  } \
//  return pimpl_->statement

#define CALL_IMPL(statement, ...) \
  pimpl_->statement

#define RETURN_IMPL(statement, error_value) \
  return pimpl_->statement

void WindowSurface::Resize(const int width, const int height)
{
  CALL_IMPL(SetSize(width, height));
}

void WindowSurface::Present() const
{
  CALL_IMPL(Present());
}

ID3D12Resource* WindowSurface::BackBuffer() const
{
  RETURN_IMPL(BackBuffer(), nullptr);
}

D3D12_CPU_DESCRIPTOR_HANDLE WindowSurface::Rtv() const
{
  RETURN_IMPL(Rtv(), {});
}

D3D12_VIEWPORT WindowSurface::Viewport() const
{
  RETURN_IMPL(Viewport(), {});
}

D3D12_RECT WindowSurface::Scissor() const
{
  RETURN_IMPL(Scissor(), {});
}

void WindowSurface::OnInitialize()
{
  Super::OnInitialize();
  CALL_IMPL(CreateSwapChain());
}

void WindowSurface::OnRelease()
{
  Super::OnRelease();
  pimpl_->DoRelease();
}

#undef CALL_IMPL
#undef RETURN_IMPL
