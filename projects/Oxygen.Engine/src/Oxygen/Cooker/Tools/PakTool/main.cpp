//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <iostream>
#include <span>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolCommands.h>

auto main(int argc, char** argv) -> int
{
  const oxygen::content::toolkit::ToolLogging logging(argc, argv);

  auto prep_fs
    = oxygen::content::pak::tool::RealRequestPreparationFileSystem {};
  auto artifact_fs = oxygen::content::pak::tool::RealArtifactFileSystem {};
  return oxygen::content::pak::tool::RunPakToolApp(
    std::span<char*>(argv, static_cast<size_t>(argc)), std::cout, std::cerr,
    prep_fs, artifact_fs);
}
