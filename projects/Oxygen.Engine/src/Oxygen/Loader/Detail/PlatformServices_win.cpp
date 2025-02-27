//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <stdexcept>
#include <string>

#include <windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Loader/Detail/PlatformServices.h>

using oxygen::loader::detail::PlatformServices;

namespace {
auto GetLastErrorAsString() -> std::string
{
    const DWORD error_code = GetLastError();
    if (error_code == 0) {
        return {};
    }

    LPSTR message_buffer = nullptr;
    const size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message_buffer), // NOLINT(*-reinterpret-cast) windows programming
        0,
        nullptr);

    std::string message;
    if (size > 0 && message_buffer != nullptr) {
        message = message_buffer;
        LocalFree(message_buffer);

        // Remove trailing newlines
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
    }

    return message;
}
} // namespace

auto PlatformServices::GetExecutableDirectory() const -> std::string
{
    std::array<char, MAX_PATH> buffer {};
    const DWORD size = GetModuleFileNameA(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));

    if (size == 0) {
        throw std::runtime_error("Failed to get executable path: " + GetLastErrorAsString());
    }

    const std::string path(buffer.data());
    const size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? "" : path.substr(0, pos + 1);
}

auto PlatformServices::LoadModule(const std::string& path) -> ModuleHandle
{
    HMODULE module = LoadLibraryA(path.c_str());
    if (module == nullptr) {
        throw std::runtime_error("Failed to load module '" + path + "': " + GetLastErrorAsString());
    }
    return static_cast<ModuleHandle>(module);
}

auto PlatformServices::OpenMainExecutableModule() -> ModuleHandle
{
    HMODULE module = GetModuleHandle(nullptr);
    DCHECK_NOTNULL_F(module, "Failed to open the main executable module: {}", GetLastErrorAsString());
    return static_cast<ModuleHandle>(module);
}

void PlatformServices::CloseModule(ModuleHandle module)
{
    if (module != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module));
    }
}

auto PlatformServices::GetModuleHandleFromReturnAddress(void* returnAddress) const -> ModuleHandle
{
    if (returnAddress == nullptr) {
        return nullptr;
    }

    HMODULE moduleHandle = nullptr;
    if (GetModuleHandleEx(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCTSTR>(returnAddress),
            &moduleHandle)) {
        return static_cast<ModuleHandle>(moduleHandle);
    }

    // If GetModuleHandleEx fails, return nullptr
    return nullptr;
}

auto PlatformServices::IsMainExecutableModule(ModuleHandle moduleHandle) const -> bool
{
    HMODULE mainModule = GetModuleHandle(nullptr);
    return mainModule == moduleHandle;
}

auto PlatformServices::GetRawFunctionAddress(
    ModuleHandle module,
    const std::string& symbol) noexcept(false) -> RawFunctionPtr
{
    if (module == nullptr) {
        throw std::invalid_argument("Module handle is null");
    }

    FARPROC proc = GetProcAddress(static_cast<HMODULE>(module), symbol.data());

    if (proc == nullptr) {
        throw std::runtime_error("Failed to resolve symbol '"
            + std::string(symbol) + "': " + GetLastErrorAsString());
    }

    // C++20 std::bit_cast is designed for this exact purpose
    return std::bit_cast<RawFunctionPtr>(proc);
}
