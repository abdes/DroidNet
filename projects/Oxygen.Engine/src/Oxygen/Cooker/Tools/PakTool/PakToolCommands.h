//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolOptions.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>

namespace oxygen::content::pak::tool {

//! The tool's identity, and its structured report of command line failures.
[[nodiscard]] auto PakToolInfo() -> oxygen::content::toolkit::ToolInfo;

//! The `build` and `patch` commands: one command, two build modes.
/*!
 Both parse into the shared `PakToolCliOptions`. Console output and the exit
 code (`PakToolExitCode`) are the tool's own contract, so a failed run returns
 its code with no message for the toolkit to print.
*/
class PakToolCommand final : public oxygen::content::toolkit::ToolCommand {
public:
  PakToolCommand(pak::BuildMode mode, PakToolCliOptions& options,
    IRequestPreparationFileSystem& prep_fs, IArtifactFileSystem& artifact_fs);

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run(oxygen::content::toolkit::ToolContext& context)
    -> oxygen::content::toolkit::ToolResult override;

private:
  pak::BuildMode mode_;
  observer_ptr<PakToolCliOptions> options_;
  observer_ptr<IRequestPreparationFileSystem> prep_fs_;
  observer_ptr<IArtifactFileSystem> artifact_fs_;
};

//! Runs the PakTool command line `argv` on the given streams and file systems.
/*!
 The seam the tests and `main` share: it builds the commands over one option
 set and returns the process exit code (`PakToolExitCode`).
*/
[[nodiscard]] auto RunPakToolApp(std::span<char*> argv, std::ostream& out,
  std::ostream& err, IRequestPreparationFileSystem& prep_fs,
  IArtifactFileSystem& artifact_fs) -> int;

} // namespace oxygen::content::pak::tool
