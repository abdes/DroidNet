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
concept FunctionPointer = std::is_pointer_v<T>
    && std::is_function_v<std::remove_pointer_t<T>>;

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
    using ModuleHandle = void*; //!< A generic alias for module handles.
    using RawFunctionPtr = void* (*)(); //!< A generic alias for raw function pointers.

    PlatformServices() = default;
    virtual ~PlatformServices() = default;

    OXYGEN_DEFAULT_COPYABLE(PlatformServices)
    OXYGEN_DEFAULT_MOVABLE(PlatformServices)

    //! Get the path to the executable directory.
    /*!
     This method is preferred to relying on argv[0], which may not be reliable
     in all cases. The path returned is guaranteed to be a directory, and it
     will always end with a path separator.
    */
    [[nodiscard]] virtual auto GetExecutableDirectory() const -> std::string;

    //! Dynamically loads a module from the given path.
    /*!
     \param path The path to the module to load. Cannot be empty, and if it
     contains a path separator, it is interpreted as a (relative or absolute)
     path. Otherwise, it is interpreted as a module name and will be looked for
     by the platform specific dynamic linker.
     \return A non-null handle to the loaded module.
     \throws std::runtime_error if the module could not be loaded. A description
     of the error is included.

     \note The specified module may cause other modules to be loaded.
    */
    [[nodiscard]] virtual auto LoadModule(const std::string& path) -> ModuleHandle;

    [[nodiscard]] virtual auto OpenMainExecutableModule() -> ModuleHandle;

    //! Closes a previously loaded module.
    /*!
     Calling this method will only decrement the reference count on the
     dynamically loaded shared object referred to by handle.

     If the object's reference count drops to zero and no symbols in this module
     are required by other modules, then the module is unloaded after first
     calling any destructors defined for the module.

     All shared objects that were automatically loaded when `LoadModule()` was
     invoked on the object referred to by handle are recursively closed in the
     same manner.
    */
    virtual void CloseModule(ModuleHandle module);

    //! Checks if the given module handle is the main executable module.
    /*!
     \param moduleHandle The handle to the module to check.
     \return `true` if the module is the main executable module, `false`
     otherwise.
    */
    [[nodiscard]] virtual auto IsMainExecutableModule(ModuleHandle moduleHandle) const -> bool;

    //! Gets a handle to the module to which the given return address (a
    //! function pointer) belongs.
    /*!
     \return A handle to the module containing the function at the given return
     address, or `nullptr` if the module could not be determined.

     The proper way to use this method is to pass the return address of a
     function, which can be obtained by calling `oxygen::ReturnAddress<>()`
     portable macro.
    */
    [[nodiscard]] virtual auto GetModuleHandleFromReturnAddress(void* returnAddress) const -> ModuleHandle;

    //! Type-safe function resolver, allowing to get a properly typed function
    //! pointer for the given symbol name.
    template <FunctionPointer T>
    [[nodiscard]] auto GetFunctionAddress(
        ModuleHandle module,
        const std::string& function_name) noexcept(false) -> T
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
    /*!
     \param module A valid handle to the module containing the symbol.
     \param symbol The name of the symbol to resolve.
     \return A non-null raw function pointer to the symbol.
     \throws std::runtime_error if the symbol could not be resolved. A
     description of the error is included.
     \throws std::invalid_argument if the module handle is `nullptr`.
    */
    [[nodiscard]] virtual auto GetRawFunctionAddress(
        ModuleHandle module, const std::string& symbol) noexcept(false) -> RawFunctionPtr;
};

} // namespace oxygen::loader::detail
