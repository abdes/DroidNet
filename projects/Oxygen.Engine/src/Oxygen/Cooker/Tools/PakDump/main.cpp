//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include "PakDumpCommand.h"

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

auto main(int argc, char** argv) -> int
{
  using oxygen::content::pakdump::PakDumpCommand;
  using oxygen::content::toolkit::RunToolMain;
  using oxygen::content::toolkit::ToolCommand;
  using oxygen::content::toolkit::ToolInfo;

  PakDumpCommand dump_command;
  std::array<ToolCommand*, 1> commands { &dump_command };

  return RunToolMain(
    ToolInfo {
      .program = "Oxygen.Cooker.PakDump",
      .version = "0.1",
      .about
      = "Developer diagnostics utility for Oxygen .pak content archives.",
    },
    commands, argc, argv);
}
