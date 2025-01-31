//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Loader/GraphicsBackendLoader.h"

#include <windows.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Graphics/Common/Graphics.h"
#include "Oxygen/Graphics/Common/Renderer.h"

using namespace oxygen::graphics;

namespace {

// This is the single instance of the loaded backend, which is wrapped in a
// shared pointer that will shutdown the backend when no longer referenced. Any
// additional references to the backend will be through a weak pointer returned
// by GetBackend(), thus ensuring that we actually have a single strong
// reference.
std::shared_ptr<oxygen::Graphics> backend_instance {};
HMODULE backend_module { nullptr };

auto GetLastErrorAsString() -> std::string
{
    const DWORD error_code = GetLastError();
    if (error_code == 0) {
        return "No error"; // No error message has been recorded
    }

    LPVOID error_message;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&error_message),
        0,
        nullptr);

    const std::wstring error_wide(static_cast<LPWSTR>(error_message));
    LocalFree(error_message);

    // Convert wide string to UTF-8
    const int size_needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        error_wide.c_str(),
        static_cast<int>(error_wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string error_str(size_needed, 0);
    WideCharToMultiByte(
        CP_UTF8,
        0,
        error_wide.c_str(),
        static_cast<int>(error_wide.size()),
        error_str.data(),
        size_needed,
        nullptr,
        nullptr);

    return error_str;
}

auto GetBackendModuleDllName(const BackendType backend) -> std::string
{
    std::string engine_name;
    switch (backend) {
    case BackendType::kDirect3D12:
        engine_name = "Direct3D12";
        break;
    case BackendType::kVulkan:
        throw std::runtime_error(
            std::string("backend not yet implemented: ") + nostd::to_string(backend));
    }
    return "Oxygen.Graphics." + engine_name + ".dll";
}

auto LoadGraphicsBackendModule(const BackendType backend_type)
    -> std::tuple<HMODULE, GraphicsModuleApi*>
{
    const auto module_name = GetBackendModuleDllName(backend_type);
    backend_module = LoadLibraryA(module_name.c_str());
    if (!backend_module) {
        const std::string error_str { GetLastErrorAsString() };
        LOG_F(ERROR, "Could not load module `{}`", module_name);
        LOG_F(ERROR, "-> {}", error_str);
        throw std::runtime_error(error_str);
    }

    // We will use a union to keep the compiler happy while we cast from the
    // FARPROC returned by GetProcAddress to the function pointer type we need.
    union {
        FARPROC proc;
        GetGraphicsModuleApiFunc func;
    } get_api;

    get_api.proc = GetProcAddress(backend_module, kGetGraphicsModuleApi);
    if (!get_api.proc) {
        const std::string error_str { GetLastErrorAsString() };
        FreeLibrary(backend_module);
        LOG_F(ERROR, "Could not find entry point `{}`", kGetGraphicsModuleApi);
        LOG_F(ERROR, "-> {}", error_str);
        throw std::runtime_error(error_str);
    }
    const auto backend_api = static_cast<GraphicsModuleApi*>(get_api.func());

    LOG_F(INFO, "Graphics backend for `{}` loaded from module `{}`",
        nostd::to_string(backend_type), module_name);

    return { backend_module, backend_api };
}

void CreateBackendInstance(GraphicsModuleApi* backend_api)
{
    // Create the backend instance, wrap it in a shared_ptr that calls the
    // backend destroy function when no longer referenced.
    backend_instance = std::shared_ptr<oxygen::Graphics>(
        static_cast<oxygen::Graphics*>(backend_api->CreateBackend()),
        [backend_api](oxygen::Graphics*) {
            backend_api->DestroyBackend();
        });
    if (!backend_instance) {
        LOG_F(ERROR, "Call to the backend API to create an instance failed");
        throw std::runtime_error("call to the backend API to create a backend failed");
    }
}

} // namespace

auto oxygen::graphics::LoadBackend(const BackendType backend) -> GraphicsPtr
{
    if (backend_instance) {
        LOG_F(WARNING, "A graphics backend has already been loaded; call UnloadBackend() first...");
        return backend_instance;
    }

    try {
        // Load the engine module for the specified backend
        auto [module, backend_api] = LoadGraphicsBackendModule(backend);
        CreateBackendInstance(backend_api);
        backend_module = module;
    } catch (const std::exception&) {
        if (backend_module)
            FreeLibrary(backend_module);
        throw;
    }

    return backend_instance;
}

void oxygen::graphics::UnloadBackend() noexcept
{
    if (!backend_module) {
        DCHECK_EQ_F(backend_instance, nullptr);
        return;
    }

    // Shutdown the backend instance if it exists and still requires shutdown.
    if (backend_instance) {
        if (backend_instance->IsInitialized())
            backend_instance->Shutdown();
        backend_instance.reset();
    }

    // Unload the backend module if it was loaded.
    FreeLibrary(backend_module);
}

auto oxygen::graphics::GetBackend() noexcept -> GraphicsPtr
{
    CHECK_NOTNULL_F(
        backend_instance,
        "No graphics backend instance has been created; call LoadBackend() first...");

    // It is important to only return a weak pointer to the backend instance, to
    // prevent it from being kept alive after UnloadBackend() is called.
    return backend_instance;
}
