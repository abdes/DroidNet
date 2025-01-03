//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Common/Graphics.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Graphics/Common/Renderer.h"

using oxygen::Graphics;

auto Graphics::GetRenderer() const noexcept -> const Renderer*
{
  CHECK_F(IsInitialized(), "graphics backend has not been initialized before being used");
  CHECK_F(is_renderer_less_, "we're running renderer-less, but some code is requesting a renderer from the graphics backend");

  return renderer_.get();
}

auto Graphics::GetRenderer() noexcept -> Renderer*
{
  CHECK_F(IsInitialized(), "graphics backend has not been initialized before being used");
  CHECK_F(!is_renderer_less_, "we're running renderer-less, but some code is requesting a renderer from the graphics backend");

  return renderer_.get();
}

auto Graphics::GetPerFrameResourceManager() const noexcept -> const renderer::PerFrameResourceManager&
{
  CHECK_F(IsInitialized(), "graphics backend has not been initialized before being used");
  CHECK_F(is_renderer_less_, "we're running renderer-less, but some code is requesting a renderer from the graphics backend");

  return renderer_->GetPerFrameResourceManager();
}

void Graphics::OnInitialize(PlatformPtr platform, const GraphicsBackendProperties& props)
{
  platform_ = std::move(platform);

  InitializeGraphicsBackend(platform, props);

  // Create and initialize the renderer instance if we are not running renderer-less.
  if (props.renderer_props) {
    is_renderer_less_ = false;
    renderer_ = CreateRenderer();
    if (renderer_) {
      renderer_->Initialize(platform_, *props.renderer_props);
    }
  }
}

void Graphics::OnShutdown()
{
  if (renderer_) {
    renderer_->Shutdown();
    renderer_.reset();
  }
  ShutdownGraphicsBackend();
}
