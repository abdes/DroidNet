//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <source_location>

#if defined(_WIN32)
#  include <Windows.h>
#else
#  include <unistd.h>
#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>

namespace {

auto SetupLogging(const char* program_name,
  const oxygen::engine::interop::LoggingConfig& config) -> void
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = config.verbosity;
  loguru::g_colorlogtostderr = config.is_colored;
  loguru::set_thread_name("engine-main");

  if (config.vmodules != nullptr) {
    // NOTE: Loguru expects argv[0] to be the program name, and argv[argc]
    // to be a sentinel `nullptr`.
    std::string vmodules = std::string("--vmodule=") + config.vmodules;
    std::vector<const char*> argv {
      program_name,
      vmodules.c_str(),
      nullptr,
    };
    auto argc = static_cast<int>(argv.size() - 1);
    loguru::init(argc, argv.data());
  }
}

} // namespace

namespace oxygen::engine::interop {

auto ConfigureLogging(const LoggingConfig& config) -> bool
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

  if (config.verbosity < loguru::Verbosity_OFF
    || config.verbosity > loguru::Verbosity_MAX) {
    report_error("Error: verbosity must be between Verbosity_OFF (-9) and "
                 "Verbosity_MAX (+9)\n");
    return false;
  }

  try {
    SetupLogging("OxygenEngine", config);
    return true;
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

auto LogInfoMessage(const char* message) -> void
{
  auto source = std::source_location::current();
  LOG_F(INFO, "{}", message);
}

} // namespace oxygen::engine::interop
