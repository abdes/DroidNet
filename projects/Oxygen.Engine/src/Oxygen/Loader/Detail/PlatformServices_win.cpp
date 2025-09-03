//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <string>

#include <windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Loader/Detail/PlatformServices.h>

using oxygen::loader::detail::PlatformServices;

namespace {

//! Converts the last Windows API error code to a human-readable string.
/*!
 Retrieves the error code from GetLastError() and formats it using
 FormatMessageA. Trailing newlines are removed from the resulting message.

 @return A formatted error message string, or empty string if no error.

 ### Performance Characteristics

 - Time Complexity: O(1) - single API call
 - Memory: Allocates temporary buffer for message formatting
 - Optimization: Uses system message formatting for localized errors
*/
auto GetLastErrorAsString() -> std::string
{
  const DWORD error_code = GetLastError();
  if (error_code == 0) {
    return {};
  }

  LPSTR message_buffer = nullptr;
  const std::size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
      | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    reinterpret_cast<LPSTR>(
      &message_buffer), // NOLINT(*-reinterpret-cast) windows programming
    0, nullptr);

  std::string message;
  if (size > 0 && message_buffer != nullptr) {
    message = message_buffer;
    LocalFree(message_buffer);

    // Remove trailing newlines using modern erase-remove idiom
    std::erase_if(message, [](const char c) { return c == '\n' || c == '\r'; });
  }

  return message;
}

//! Safely retrieves the filename of a module with buffer size handling.
/*!
 Gets the module filename using GetModuleFileNameA with proper buffer
 management. Starts with MAX_PATH but grows the buffer if needed. Returns only
 the filename part without the directory path.

 @param module The module handle to get the filename for.
 @return The filename of the module, or "<unknown module>" on failure.

 ### Performance Characteristics

 - Time Complexity: O(1) - typically single API call
 - Memory: May allocate up to 32KB buffer for very long paths
 - Optimization: Starts with optimal buffer size, grows only if needed
*/
auto GetModuleFileNameSafe(const HMODULE module) -> std::string
{
  // Start with MAX_PATH, but be prepared to grow if needed
  constexpr std::size_t initial_buffer_size = MAX_PATH;

  std::string buffer;
  buffer.resize(initial_buffer_size);

  DWORD size = GetModuleFileNameA(
    module, std::data(buffer), static_cast<DWORD>(std::size(buffer)));

  // Check if the buffer was too small
  if (size == std::size(buffer)
    && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    constexpr std::size_t max_buffer_size = 32768;
    // Try with a larger buffer
    buffer.resize(max_buffer_size);
    size = GetModuleFileNameA(
      module, std::data(buffer), static_cast<DWORD>(std::size(buffer)));
  }

  if (size == 0) {
    return "<unknown module>";
  }

  // Resize to actual content length
  buffer.resize(size);

  // Extract only the filename part (without directory path)
  if (const auto pos = buffer.find_last_of("\\/"); pos != std::string::npos) {
    return buffer.substr(pos + 1);
  }
  return buffer;
}

} // namespace

/*!
 Gets the directory containing the current executable by calling
 GetModuleFileNameA with proper buffer management and error handling. Handles
 long paths by growing the buffer when needed.

 @return The directory path containing the executable, ending with a path
         separator.
 @throw std::runtime_error if the executable path could not be determined.

### Performance Characteristics

 - Time Complexity: O(1) - typically single system call
 - Memory: May allocate up to 32KB for very long paths
 - Optimization: Uses efficient buffer resizing strategy

 ### Usage Examples

 ```cpp
 PlatformServices services;
 std::string exe_dir = services.GetExecutableDirectory();
 std::string config_path = exe_dir + "config.xml";
 ```
*/
auto PlatformServices::GetExecutableDirectory() const -> std::string
{
  // Start with MAX_PATH, but be prepared to grow if needed
  constexpr std::size_t initial_buffer_size = MAX_PATH;

  std::string buffer;
  buffer.resize(initial_buffer_size);

  DWORD size = GetModuleFileNameA(
    nullptr, std::data(buffer), static_cast<DWORD>(std::size(buffer)));

  // Check if the buffer was too small
  if (size == std::size(buffer)
    && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    constexpr std::size_t max_buffer_size = 32768;
    // Try with a larger buffer
    buffer.resize(max_buffer_size);
    size = GetModuleFileNameA(
      nullptr, std::data(buffer), static_cast<DWORD>(std::size(buffer)));
  }

  if (size == 0) {
    throw std::runtime_error(
      "Failed to get executable path: " + GetLastErrorAsString());
  }

  // Resize to actual content length
  buffer.resize(size);
  if (const auto pos = buffer.find_last_of("\\/"); pos != std::string::npos) {
    return buffer.substr(0, pos + 1);
  }
  return {};
}

/*!
 Loads a dynamic library module using LoadLibraryA. The module can be specified
 as either a full path or just a module name (which will be searched in the
 standard locations).

 @param path The path or name of the module to load.
 @return A handle to the loaded module.
 @throw std::runtime_error if the module could not be loaded, with detailed
                           error information from the system.

 ### Performance Characteristics

 - Time Complexity: O(n) where n is the size of loaded dependencies
 - Memory: Module memory plus any dependencies loaded automatically
 - Optimization: Uses Windows loader cache for already-loaded modules

 ### Usage Examples

 ```cpp
 PlatformServices services;
 auto handle = services.LoadModule("user32.dll");
 // Module is now loaded and ready for symbol resolution
 ```

 @note Loading a module may cause other dependent modules to be loaded
       automatically by the Windows loader.
*/
auto PlatformServices::LoadModule(const std::string& path) -> ModuleHandle
{
  HMODULE module = LoadLibraryA(path.c_str());
  if (module == nullptr) {
    throw std::runtime_error(
      "Failed to load module '" + path + "': " + GetLastErrorAsString());
  }
  return module;
}

/*!
 Opens the main executable module using GetModuleHandle with nullptr parameter.
 This provides access to symbols exported by the main executable.

 @return A handle to the main executable module.
 @throw std::runtime_error if the main executable module could not be opened
                           (which should be extremely rare).

 ### Performance Characteristics

 - Time Complexity: O(1) - direct system call
 - Memory: No additional allocation
 - Optimization: Uses cached handle from Windows loader

 ### Usage Examples

 ```cpp
 PlatformServices services;
 auto main_handle = services.OpenMainExecutableModule();
 auto func = services.GetFunctionAddress<MyFunc>(main_handle, "my_export");
 ```
*/
auto PlatformServices::OpenMainExecutableModule() -> ModuleHandle
{
  HMODULE module = GetModuleHandle(nullptr);
  DCHECK_NOTNULL_F(module, "Failed to open the main executable module: {}",
    GetLastErrorAsString());
  return module;
}

/*!
 Closes a module handle by calling FreeLibrary. This decrements the reference
 count and may unload the module if no other references exist. Handles nullptr
 gracefully by doing nothing.

 @param module The module handle to close, or nullptr to do nothing.

### Performance Characteristics

 - Time Complexity: O(1) for the call, O(n) if module destructors run
 - Memory: May free module memory and dependencies
 - Optimization: Reference counting prevents premature unloading

 ### Usage Examples

 ```cpp
 auto handle = services.LoadModule("some_module.dll");
 // Use the module...
 services.CloseModule(handle); // Safe to call even if handle is nullptr
 ```

 @note This method logs module unloading at verbosity level 1 for debugging.
*/
auto PlatformServices::CloseModule(const ModuleHandle module) -> void
{
  if (module != nullptr) {
    LOG_F(1, "unload module: {}",
      GetModuleFileNameSafe(static_cast<HMODULE>(module)));
    FreeLibrary(static_cast<HMODULE>(module));
  }
}

/*!
 Determines which module contains a given function address using
 GetModuleHandleEx. This is useful for finding the module that contains a
 specific function when you only have its address.

 @param returnAddress The address of a function to locate. Can be nullptr.
 @return The module handle containing the function, or nullptr if not found or
         if returnAddress is nullptr.

 ### Performance Characteristics

 - Time Complexity: O(1) - direct system call
 - Memory: No allocation
 - Optimization: Uses Windows fast address-to-module lookup

 ### Usage Examples

 ```cpp
 void* address = reinterpret_cast<void*>(&some_function);
 auto module = services.GetModuleHandleFromReturnAddress(address);
 if (module) {
     // Found the module containing some_function
 }
 ```

 @note Uses GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT to avoid affecting the
       module's reference count.
*/
auto PlatformServices::GetModuleHandleFromReturnAddress(
  void* returnAddress) const -> ModuleHandle
{
  if (returnAddress == nullptr) {
    return nullptr;
  }

  HMODULE moduleHandle = nullptr;
  if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCTSTR>(returnAddress), &moduleHandle)) {
    return moduleHandle;
  }

  // If GetModuleHandleEx fails, return nullptr
  return nullptr;
}

/*!
 Checks if the given module handle refers to the main executable by comparing it
 with the handle returned by GetModuleHandle(nullptr).

 @param moduleHandle The module handle to check.
 @return `true` if the module is the main executable, `false` otherwise.

### Performance Characteristics

 - Time Complexity: O(1) - simple pointer comparison
 - Memory: No allocation
 - Optimization: Direct handle comparison after single system call

 ### Usage Examples

 ```cpp
 auto some_module = services.LoadModule("library.dll");
 auto main_module = services.OpenMainExecutableModule();

 bool is_main = services.IsMainExecutableModule(main_module); // true
 bool is_lib = services.IsMainExecutableModule(some_module);  // false
 ```
*/
auto PlatformServices::IsMainExecutableModule(
  const ModuleHandle moduleHandle) const -> bool
{
  HMODULE mainModule = GetModuleHandle(nullptr);
  return mainModule == moduleHandle;
}

/*!
 Resolves a symbol name to a raw function pointer using GetProcAddress. Performs
 validation of the module handle and provides detailed error information on
 failure.

 @param module The module handle containing the symbol. Must not be nullptr.
 @param symbol The name of the symbol to resolve.
 @return A raw function pointer to the resolved symbol.
 @throw std::invalid_argument if the module handle is nullptr.
 @throw std::runtime_error if the symbol could not be resolved, with detailed
                           error information.

 ### Performance Characteristics

 - Time Complexity: O(log n) where n is the number of exported symbols
 - Memory: No allocation
 - Optimization: Uses Windows symbol table hash lookup

 ### Usage Examples

 ```cpp
 auto module = services.LoadModule("kernel32.dll");
 auto raw_func = services.GetRawFunctionAddress(module, "GetCurrentThreadId");
 // raw_func now points to the GetCurrentThreadId function
 ```

 @note The returned pointer is cast using std::bit_cast for type safety and to
       avoid undefined behavior.
*/
auto PlatformServices::GetRawFunctionAddress(const ModuleHandle module,
  const std::string& symbol) noexcept(false) -> RawFunctionPtr
{
  if (module == nullptr) {
    throw std::invalid_argument("Module handle is null");
  }

  FARPROC proc
    = GetProcAddress(static_cast<HMODULE>(module), std::data(symbol));

  if (proc == nullptr) {
    throw std::runtime_error(
      "Failed to resolve symbol '" + symbol + "': " + GetLastErrorAsString());
  }

  // C++20 std::bit_cast is designed for this exact purpose
  // NOLINTNEXTLINE(bugprone-bitwise-pointer-cast)
  return std::bit_cast<RawFunctionPtr>(proc);
}
