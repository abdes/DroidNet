//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/TypeSystem.h"

#if defined(_WIN32) || defined(_WIN64)
#define EXPORT_SYMBOL __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define EXPORT_SYMBOL __attribute__((visibility("default")))
#else
#define EXPORT_SYMBOL
#endif

extern "C" bool initialize_called { false };

namespace {

extern "C" {
EXPORT_SYMBOL oxygen::TypeRegistry* InitializeTypeRegistry()
{
  // Single instance of the type registry provided by the main executable module.
  static oxygen::TypeRegistry registry {};

  initialize_called = true;
  return &registry;
}
} // extern "C"

} // namespace

int main(int argc, char** argv)
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
  loguru::init(argc, argv);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
