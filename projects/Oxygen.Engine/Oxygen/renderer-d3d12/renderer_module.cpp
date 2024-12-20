//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/renderer/renderer_module.h"

#include "oxygen/renderer-d3d12/renderer.h"

using oxygen::renderer::direct3d12::Renderer;

namespace {

  void* CreateRenderer()
  {
    return new Renderer();
  }

  void DestroyRenderer(void* object)
  {
    if (!object) return;
    auto* renderer = static_cast<Renderer*>(object);
    renderer->Shutdown();
    delete renderer;
  }

} // namespace

extern "C" __declspec(dllexport) void* GetRendererModuleApi()
{
  static oxygen::graphics::RendererModuleInterface render_module;
  render_module.CreateRenderer = CreateRenderer;
  render_module.DestroyRenderer = DestroyRenderer;
  return &render_module;
}
