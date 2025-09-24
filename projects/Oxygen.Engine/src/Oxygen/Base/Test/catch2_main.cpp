//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include <catch2/catch_session.hpp>

auto main(int argc, char** argv) -> int
{
  // Check the program was started just for test case discovery
  bool list_tests = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strstr(argv[i], "list-tests") != nullptr) {
      list_tests = true;
      break;
    }
  }

  if (!list_tests) {
#if defined(_MSC_VER)
    // Enable memory leak detection in debug mode
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::init(argc, const_cast<const char**>(argv));
    loguru::g_stderr_verbosity = loguru::Verbosity_1;
  }

  const int result = Catch::Session().run(argc, argv);

  if (!list_tests) {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::shutdown();
  }

  return result;
}
