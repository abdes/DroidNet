//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Loader/RendererLoader.h"

#include <windows.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Renderers/Common/Renderer.h"

using namespace oxygen::graphics;

namespace {

  // This is the single instance of the loaded renderer, which is wrapped in a
  // shared pointer that will call the backend destroy function when no longer
  // referenced. any additional reference to the renderer will be through a weak
  // pointer returned by GetRenderer(), thus ensuring that we actually have a
  // single strong reference.
  std::shared_ptr<oxygen::Renderer> renderer_instance{};

  auto GetLastErrorAsString() -> std::string
  {
    const DWORD error_code = GetLastError();
    if (error_code == 0) {
      return "No error"; // No error message has been recorded
    }

    LPVOID error_message;
    FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error_code,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&error_message),
      0,
      nullptr
    );

    std::wstring error_wstr(static_cast<LPWSTR>(error_message));
    LocalFree(error_message);

    // Convert wide string to UTF-8
    const int size_needed = WideCharToMultiByte(
      CP_UTF8,
      0,
      error_wstr.c_str(),
      static_cast<int>(error_wstr.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
    std::string error_str(size_needed, 0);
    WideCharToMultiByte(
      CP_UTF8,
      0,
      error_wstr.c_str(),
      static_cast<int>(error_wstr.size()),
      error_str.data(),
      size_needed,
      nullptr,
      nullptr);

    return error_str;
  }

  auto GetEngineModuleDllName(const GraphicsBackendType backend) -> std::string
  {
    std::string engine_name;
    switch (backend) {
    case GraphicsBackendType::kDirect3D12:
      engine_name = "Direct3D12";
      break;
    case GraphicsBackendType::kVulkan:
      throw std::runtime_error(
        "backend not yet implemented: " + nostd::to_string(backend));
    }
    return "DroidNet.Oxygen.Renderer." + engine_name + ".dll";
  }

  auto LoadEngineModule(const GraphicsBackendType backend)
    -> std::tuple<HMODULE, RendererModuleApi*> {
    const auto module_name = GetEngineModuleDllName(backend);
    const HMODULE renderer_module = LoadLibraryA(module_name.c_str());
    if (!renderer_module) {
      const std::string error_str{ GetLastErrorAsString() };
      LOG_F(ERROR, "Could not load module `{}`", module_name);
      LOG_F(ERROR, "-> {}", error_str);
      throw std::runtime_error(error_str);
    }

    // We will use a union to keep the compiler happy while we cast from the
    // FARPROC returned by GetProcAddress to the function pointer type we need.
    union
    {
      FARPROC proc;
      GetRendererModuleApiFunc func;
    } get_api;

    get_api.proc = GetProcAddress(renderer_module, kGetRendererModuleApi);
    if (!get_api.proc) {
      const std::string error_str{ GetLastErrorAsString() };
      FreeLibrary(renderer_module);
      LOG_F(ERROR, "Could not find entry point `{}`", kGetRendererModuleApi);
      LOG_F(ERROR, "-> {}", error_str);
      throw std::runtime_error(error_str);
    }
    const auto backend_api = static_cast<RendererModuleApi*>(get_api.func());

    LOG_F(INFO, "Render backend for `{}` loaded from module `{}`",
          nostd::to_string(backend), module_name);

    return { renderer_module, backend_api };
  }

  void CreateRendererInstance(RendererModuleApi* backend_api)
  {
    // Create the renderer instance, wrap it in a shared_ptr that calls the
    // backend's destroy function when no longer referenced.
    renderer_instance = std::shared_ptr<oxygen::Renderer>(
      static_cast<oxygen::Renderer*>(backend_api->CreateRenderer()),
      [backend_api](oxygen::Renderer*) {
        backend_api->DestroyRenderer();
      });
    if (!renderer_instance) {
      LOG_F(ERROR, "Call to the backend API to create a renderer failed");
      throw std::runtime_error("call to the backend API to create a renderer failed");
    }
  }

  void InitializeRenderer(
    oxygen::PlatformPtr platform,
    const oxygen::RendererProperties& renderer_props)
  {
    try {
      renderer_instance->Initialize(std::move(platform), renderer_props);
    }
    catch (const std::exception& e) {
      renderer_instance.reset();
      LOG_F(ERROR, "Render loaded, but failed to initialize properly: {}", e.what());
      throw;
    }
  }

}  // namespace

void oxygen::graphics::CreateRenderer(
  const GraphicsBackendType backend,
  PlatformPtr platform,
  const RendererProperties& renderer_props)
{
  if (renderer_instance) {
    LOG_F(WARNING, "A renderer instance already exists; call DestroyRenderer() first...");
    return;
  }

  // Load the engine module for the specified backend
  auto [renderer_module, backend_api] = LoadEngineModule(backend);

  try {
    CreateRendererInstance(backend_api);
    InitializeRenderer(std::move(platform), renderer_props);
  }
  catch (const std::exception&) {
    FreeLibrary(renderer_module);
    throw;
  }
}

void oxygen::graphics::DestroyRenderer() noexcept
{
  DCHECK_NOTNULL_F(
    renderer_instance,
    "No renderer instance has been created; call CreateRenderer() first...");

  if (!renderer_instance) {
    LOG_F(WARNING, "No renderer instance has been created; call CreateRenderer() first...");
    return;
  }

  // Shutdown the renderer instance
  try {
    renderer_instance->Shutdown();
  }
  catch (const std::exception& e) {
    LOG_F(WARNING, "Render shutdown was incomplete: {}", e.what());
  }

  // Reset the shared pointer to the renderer instance, which will call the
  // backend's destroy function, and make any further locks of weak pointers
  // returned by GetRenderer() to fail.
  renderer_instance.reset();
}

oxygen::RendererPtr oxygen::graphics::GetRenderer() noexcept
{
  CHECK_NOTNULL_F(
    renderer_instance,
    "No renderer instance has been created; call CreateRenderer() first...");

  // It is important to only return a weak pointer to the renderer instance, to
  // prevent it from being kept alive after DestroyRenderer() is called.
  return renderer_instance;
}
