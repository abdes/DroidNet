//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ReturnAddress.h>
#include <Oxygen/Loader/Detail/PlatformServices.h>

using oxygen::loader::detail::PlatformServices;

namespace {

NOLINT_TEST(PlatformServicesTest, GetExecutableDirectory_ReturnsValidPath)
{
    PlatformServices services;

    std::string result = services.GetExecutableDirectory();

    // Verify the result is not empty
    EXPECT_FALSE(result.empty());

    // Verify it ends with a path separator (Windows: '\', Unix: '/')
    char preferred_separator = std::filesystem::path::preferred_separator;
    EXPECT_EQ(result.back(), preferred_separator);

    // Verify the directory exists
    std::filesystem::path dir_path(result);
    EXPECT_TRUE(std::filesystem::exists(dir_path));
    EXPECT_TRUE(std::filesystem::is_directory(dir_path));
}

NOLINT_TEST(PlatformServicesTest, MainExecutableModuleHandling)
{
    PlatformServices services;

    // Get handle to main executable
    auto* main_module = services.OpenMainExecutableModule();

    // Check that the handle is not null
    EXPECT_NE(main_module, nullptr);

    // Verify that IsMainExecutableModule returns true for the main module
    EXPECT_TRUE(services.IsMainExecutableModule(main_module));

    // Don't need to close this since GetModuleHandle doesn't increment ref count
}

NOLINT_TEST(PlatformServicesTest, GetModuleHandleFromReturnAddressWithNullptr)
{
    PlatformServices services;

    // Passing nullptr should return nullptr
    auto* module_handle = services.GetModuleHandleFromReturnAddress(nullptr);
    EXPECT_EQ(module_handle, nullptr);
}

NOLINT_TEST(PlatformServicesTest, GetModuleHandleFromReturnAddressWithCurrentFunction)
{
    PlatformServices services;

    // Get the module handle from the current function's return address
    void* return_address = oxygen::ReturnAddress<>();
    auto* module_handle = services.GetModuleHandleFromReturnAddress(return_address);

    // The module handle should not be null
    EXPECT_NE(module_handle, nullptr);

    // It should be the main executable module
    EXPECT_TRUE(services.IsMainExecutableModule(module_handle));
}

NOLINT_TEST(PlatformServicesTest, LoadAndCloseSystemLibrary)
{
    PlatformServices services;

// Load a system library that should be available on all platforms
#ifdef _WIN32
    const std::string library_name = "kernel32.dll";
#elif defined(__APPLE__)
    const std::string library_name = "libSystem.dylib";
#else
    const std::string library_name = "libdl.so";
#endif

    // Load the library
    auto* module_handle = services.LoadModule(library_name);
    EXPECT_NE(module_handle, nullptr);

    // This shouldn't be the main executable
    EXPECT_FALSE(services.IsMainExecutableModule(module_handle));

    // Close the library - should not crash
    services.CloseModule(module_handle);
}

NOLINT_TEST(PlatformServicesTest, GetFunctionAddressFromSystemLibrary)
{
    PlatformServices services;

// Load a system library and look for a function that exists on all platforms
#ifdef _WIN32
    const std::string library_name = "kernel32.dll";
    const std::string function_name = "GetCurrentProcess";
    using FunctionType = void* (*)();
#elif defined(__APPLE__)
    const std::string library_name = "libSystem.dylib";
    const std::string function_name = "malloc";
    using FunctionType = void* (*)(size_t);
#else
    const std::string library_name = "libc.so.6";
    const std::string function_name = "malloc";
    using FunctionType = void* (*)(size_t);
#endif

    // Load the library
    auto* module_handle = services.LoadModule(library_name);
    EXPECT_NE(module_handle, nullptr);

    // Get function address
    auto function_ptr = services.GetFunctionAddress<FunctionType>(module_handle, function_name);
    EXPECT_NE(function_ptr, nullptr);

    // Close the library
    services.CloseModule(module_handle);
}

NOLINT_TEST(PlatformServicesTest, GetFunctionAddressWithNullModule_Fails)
{
    PlatformServices services;

    // Attempting to get a function from a null module should throw
    EXPECT_THROW(
        { [[maybe_unused]] auto _ = services.GetFunctionAddress<void (*)()>(nullptr, "SomeFunction"); },
        std::invalid_argument);
}

NOLINT_TEST(PlatformServicesTest, GetFunctionAddressWithInvalidSymbol_Fails)
{
    PlatformServices services;

// Load a system library
#ifdef _WIN32
    const std::string library_name = "kernel32.dll";
    using FunctionType = void* (*)();
#elif defined(__APPLE__)
    const std::string library_name = "libSystem.dylib";
    using FunctionType = void* (*)(size_t);
#else
    const std::string library_name = "libc.so.6";
    using FunctionType = void* (*)(size_t);
#endif

    auto* module_handle = services.LoadModule(library_name);
    EXPECT_NE(module_handle, nullptr);

    // Try to get a function that definitely doesn't exist
    const std::string non_existent_function = "ThisFunctionDefinitelyDoesNotExist_XYZ123";
    EXPECT_THROW(
        { [[maybe_unused]] auto _ = services.GetFunctionAddress<FunctionType>(module_handle, non_existent_function); },
        std::runtime_error);

    services.CloseModule(module_handle);
}

NOLINT_TEST(PlatformServicesTest, CallGetFunctionAddressResult)
{
    PlatformServices services;

#ifdef _WIN32
    const std::string library_name = "kernel32.dll";
    const std::string function_name = "GetCurrentProcessId";
    using ProcessIdFn = uint32_t (*)();
#elif defined(__APPLE__)
    const std::string library_name = "libSystem.dylib";
    const std::string function_name = "getpid";
    using ProcessIdFn = int (*)();
#else
    const std::string library_name = "libc.so.6";
    const std::string function_name = "getpid";
    using ProcessIdFn = int (*)();
#endif

    // Load the library
    auto* module_handle = services.LoadModule(library_name);
    EXPECT_NE(module_handle, nullptr);

    // Get function address with the correct function signature
    auto get_process_id = services.GetFunctionAddress<ProcessIdFn>(module_handle, function_name);
    EXPECT_NE(get_process_id, nullptr);

    // Actually call the function to verify it works
    auto process_id = get_process_id();
    EXPECT_GT(process_id, 0);

    services.CloseModule(module_handle);
}

} // namespace
