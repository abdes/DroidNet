//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <crtdbg.h>
#include <cstdlib>
#include <span>
#include <string_view>


#if defined(_WIN32)
#  include <Windows.h>
#else
#  include <unistd.h>
#endif

#include <Oxygen/Base/Logging.h>

// When not building with shared libraries / DLLs, make sure to link the main
// executable (statically) with the type registry initialization library:
//
// target_link_libraries(
//   ${META_MODULE_TARGET}
//   PRIVATE
//     $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::ts-init>
// )
//
// The linker will optimize the library out if it is not being used, so we force
// link it by calling its initialization function and storing the result in an
// unused variable.
namespace oxygen {
class TypeRegistry;
} // namespace oxygen
extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
namespace {
[[maybe_unused]] const auto* const ts_registry_unused = InitializeTypeRegistry();
} // namespace

// The real main entry point for the application.
extern "C" void MainImpl(std::span<const char*> args);

auto main(int argc, char** argv) noexcept -> int
{
#if defined(_MSC_VER)
    // Enable memory leak detection in debug mode
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // Pre-allocate static error messages when we are handling critical failures
    constexpr std::string_view kUnhandledException = "Error: Out of memory or other critical failure when logging unhandled exception\n";
    constexpr std::string_view kUnknownUnhandledException = "Error: Out of memory or other critical failure when logging unhandled exception of unknown type\n";

    // Low-level error reporting function that won't allocate memory
    auto report_error = [](std::string_view message) noexcept {
#if defined(_WIN32)
        HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
        DWORD bytes_written { 0UL };
        WriteFile(stderr_handle, message.data(), static_cast<DWORD>(message.size()), &bytes_written, nullptr);
#else
        write(STDERR_FILENO, message.data(), message.size());
#endif
    };

    int exit_code = EXIT_FAILURE;

    try {
        loguru::g_preamble_date = false;
        loguru::g_preamble_file = true;
        loguru::g_preamble_verbose = false;
        loguru::g_preamble_time = false;
        loguru::g_preamble_uptime = false;
        loguru::g_preamble_thread = false;
        loguru::g_preamble_header = false;
        loguru::g_stderr_verbosity = loguru::Verbosity_1;
        loguru::g_colorlogtostderr = true;
        // Optional, but useful to time-stamp the start of the log.
        // Will also detect verbosity level on command line as -v.
        loguru::init(argc, argv);

        MainImpl(std::span(const_cast<const char**>(argv), static_cast<size_t>(argc)));
        exit_code = EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        try {
            LOG_F(ERROR, "Unhandled exception: {}", ex.what());
        } catch (...) {
            report_error(kUnhandledException);
        }
    } catch (...) {
        // Catch any other exceptions
        try {
            LOG_F(ERROR, "Unhandled exception of unknown type");
        } catch (...) {
            // Cannot do anything if ex.what() throws
            report_error(kUnknownUnhandledException);
        }
    }

    try {
        loguru::shutdown();
    } catch (...) {
        // Ignore
        (void)0;
    }

    return exit_code;
}
