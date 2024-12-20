//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/renderer-loader/renderer_loader.h"

#include <windows.h>

#include "oxygen/base/logging.h"
#include "oxygen/renderer/renderer.h"

namespace {
  std::shared_ptr<oxygen::Renderer> renderer{};
}

void oxygen::graphics::CreateRenderer(
  GraphicsBackendType backend,
  PlatformPtr platform,
  const RendererProperties& renderer_props)
{
  constexpr auto kGetRendererModuleApi = "GetRendererModuleApi";

  std::string module_name;
  switch (backend) {
  case GraphicsBackendType::kDirect3D12:
    module_name = "DroidNet.Oxygen.Renderer.Direct3D12.dll";
    break;
  case GraphicsBackendType::kVulkan:
    throw std::runtime_error(
      "backend not yet implemented: " + nostd::to_string(backend));
  }

  const HMODULE renderer_module = LoadLibraryA(module_name.c_str());
  if (!renderer_module) {
    throw std::runtime_error("could not load module: " + module_name);
  }

  union
  {
    FARPROC proc;
    GetRendererModuleInterfaceFunc func;
  } get_renderer_module_api;

  get_renderer_module_api.proc = GetProcAddress(renderer_module, kGetRendererModuleApi);
  if (!get_renderer_module_api.proc) {
    throw std::runtime_error(
      "could not find entry point: " + std::string(kGetRendererModuleApi));
  }

  LOG_F(INFO, "Render backend for `{}` loaded from module `{}`",
        nostd::to_string(backend), module_name);

  const auto backend_api = static_cast<RendererModuleInterface*>(get_renderer_module_api.func());
  if (!backend_api) {
    throw std::runtime_error("failed to get the renderer backend api");
  }

  // Create the renderer instance, wrap it in a shared_ptr that calls the
  // backend's destroy function when no longer referenced.
  renderer = std::shared_ptr<Renderer>(
    static_cast<Renderer*>(backend_api->CreateRenderer()),
    [backend_api](Renderer* ptr) {
      backend_api->DestroyRenderer();
    });
  if (!renderer) {
    throw std::runtime_error("failed to get an instance of the renderer backend");
  }

  renderer->Init(std::move(platform), renderer_props);
}

void oxygen::graphics::DestroyRenderer()
{
  renderer->Shutdown();
  renderer.reset();
}

oxygen::RendererPtr oxygen::graphics::GetRenderer()
{
  return renderer;
}
