//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <exception>

#if defined(_WIN32)
#  include <Windows.h>
#else
#  include <unistd.h>
#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>

namespace oxygen::engine::interop {

auto CreateEngine(const EngineConfig& config) -> bool
{
  // Pre-allocate static error messages when we are handling critical failures
  constexpr std::string_view kUnhandledException
    = "Error: Out of memory or other critical failure when logging unhandled "
      "exception\n";
  constexpr std::string_view kUnknownUnhandledException
    = "Error: Out of memory or other critical failure when logging unhandled "
      "exception of unknown type\n";

  // Low-level error reporting function that won't allocate memory
  auto report_error = [](std::string_view message) noexcept {
#if defined(_WIN32)
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD bytes_written { 0UL };
    WriteFile(stderr_handle, message.data(), static_cast<DWORD>(message.size()),
      &bytes_written, nullptr);
#else
    write(STDERR_FILENO, message.data(), message.size());
#endif
  };

  try {

    return true;

    // MainImpl(
    //   std::span(const_cast<const char**>(argv),
    //   static_cast<size_t>(argc)));
    // exit_code = EXIT_SUCCESS;
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

  return false;
}

} // namespace oxygen::engine::interop
