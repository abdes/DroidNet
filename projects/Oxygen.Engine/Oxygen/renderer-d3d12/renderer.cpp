//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/renderer-d3d12/renderer.h"

#include <memory>

#include "oxygen/renderer-d3d12/detail/renderer_impl.h"

using oxygen::renderer::direct3d12::Renderer;
using oxygen::renderer::direct3d12::detail::RendererImpl;

void Renderer::Init(PlatformPtr platform, const RendererProperties& props)
{
  pimpl_ = std::make_unique<RendererImpl>(platform, props);
  pimpl_->Init();
  LOG_F(INFO, "Renderer `{}` initialized", Name());
}

void Renderer::DoShutdown()
{
  pimpl_->Shutdown();
  LOG_F(INFO, "Renderer `{}` shut down", Name());
}

void Renderer::Render()
{
  pimpl_->Render();
}
