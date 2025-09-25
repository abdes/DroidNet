//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Macros.h>

namespace oxygen::loader::detail {

//! C++20 concept to constrain function pointer types.
template <typename T>
concept FunctionPointer
  = std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>;

//! Platform-specific services for the loader.
/*!
 The PlatformServices class provides a platform-independent interface for
 loading modules and resolving functions. Although it is designed with virtual
 methods, it is not intended to be subclassed, unless it is for mocking in unit
 tests.

 Instead of subclassing for each platform, we chose to have multiple
 implementation files, and only one is selected at build time.
*/
class PlatformServices {
public:
  //! A generic alias for module handles.
  using ModuleHandle = void*;
  //! A generic alias for raw function pointers.
  using RawFunctionPtr = void* (*)();

  PlatformServices() = default;
  virtual ~PlatformServices() = default;

  OXYGEN_DEFAULT_COPYABLE(PlatformServices)
  OXYGEN_DEFAULT_MOVABLE(PlatformServices)

  //! Get the path to the executable directory.
  [[nodiscard]] virtual auto GetExecutableDirectory() const -> std::string;

  //! Get the directory path of a loaded module handle (including trailing
  //! separator). Returns empty string if handle is null or path cannot be
  //! determined.
  [[nodiscard]] virtual auto GetModuleDirectory(ModuleHandle module) const
    -> std::string;

  //! Dynamically loads a module from the given path.
  [[nodiscard]] virtual auto LoadModule(const std::string& path)
    -> ModuleHandle;

  //! Opens the main executable module.
  [[nodiscard]] virtual auto OpenMainExecutableModule() -> ModuleHandle;

  //! Closes a previously loaded module.
  virtual void CloseModule(ModuleHandle module);

  //! Checks if the given module handle is the main executable module.
  [[nodiscard]] virtual auto IsMainExecutableModule(
    ModuleHandle moduleHandle) const -> bool;

  //! Gets a handle to the module containing the given return address.
  [[nodiscard]] virtual auto GetModuleHandleFromReturnAddress(
    void* returnAddress) const -> ModuleHandle;

  //! Type-safe function resolver with properly typed function pointer return.
  /*!
   Allows getting a properly typed function pointer for the given symbol name.
   This template method provides type safety by ensuring the returned function
   pointer matches the expected signature.

   @tparam T The function pointer type to resolve. Must satisfy the
             FunctionPointer concept.
   @param module The handle to the module containing the symbol.
   @param function_name The name of the symbol to resolve.
   @return A typed function pointer to the resolved symbol.
   @throw std::runtime_error if the symbol could not be resolved.
   @throw std::invalid_argument if the module handle is `nullptr`.

  ### Usage Examples

   ```cpp
   using MyFuncType = int (*)(const char*);
   auto func = platform_services.GetFunctionAddress<MyFuncType>(
       module, "my_function");
   int result = func("hello");
   ```

   @see GetRawFunctionAddress
  */
  template <FunctionPointer T>
  [[nodiscard]] auto GetFunctionAddress(
    ModuleHandle module, const std::string& function_name) noexcept(false) -> T
  {
    // Use a union to perform the cast, avoiding static analysis warnings.
    union {
      RawFunctionPtr raw;
      T typed;
    } function_ptr {};

    function_ptr.raw = GetRawFunctionAddress(module, function_name);
    return function_ptr.typed;
  }

protected:
  //! Gets a raw function pointer for the given symbol name.
  [[nodiscard]] virtual auto GetRawFunctionAddress(ModuleHandle module,
    const std::string& symbol) noexcept(false) -> RawFunctionPtr;
};

} // namespace oxygen::loader::detail
