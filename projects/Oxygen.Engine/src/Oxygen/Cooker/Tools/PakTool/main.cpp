//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Tools/PakTool/App.h>

auto main(int argc, char** argv) -> int
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = true;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  auto prep_fs
    = oxygen::content::pak::tool::RealRequestPreparationFileSystem {};
  auto artifact_fs = oxygen::content::pak::tool::RealArtifactFileSystem {};
  return oxygen::content::pak::tool::RunPakToolApp(
    std::span<char*>(argv, static_cast<size_t>(argc)), std::cout, std::cerr,
    prep_fs, artifact_fs);
}
