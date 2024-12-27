//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/logging.h"
#include "Oxygen/Renderers/Common/RendererModule.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"

namespace {

  std::shared_ptr<oxygen::renderer::d3d12::Renderer>& GetRendererInternal() {
    static auto renderer = std::make_shared<oxygen::renderer::d3d12::Renderer>();
    return renderer;
  }

  void* CreateRenderer()
  {
    return GetRendererInternal().get();
  }

  void DestroyRenderer()
  {
    auto& renderer = GetRendererInternal();
    renderer.reset();
  }

} // namespace

namespace oxygen::renderer::d3d12::detail {
  auto GetRenderer() -> Renderer&
  {
    CHECK_NOTNULL_F(GetRendererInternal());
    return *GetRendererInternal();
  }

  auto GetPerFrameResourceManager() -> renderer::PerFrameResourceManager&
  {
    return GetRendererInternal()->GetPerFrameResourceManager();
  }
}  // namespace oxygen::renderer::d3d12

extern "C" __declspec(dllexport) void* GetRendererModuleApi()
{
  static oxygen::graphics::RendererModuleApi render_module;
  render_module.CreateRenderer = CreateRenderer;
  render_module.DestroyRenderer = DestroyRenderer;
  return &render_module;
}
