//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolOptions.h>

namespace oxygen::content::pak::tool {

//! The `build` command, parsing into `options`.
[[nodiscard]] auto BuildBuildCommand(PakToolCliOptions& options)
  -> std::shared_ptr<oxygen::clap::Command>;

//! The `patch` command, parsing into `options`.
[[nodiscard]] auto BuildPatchCommand(PakToolCliOptions& options)
  -> std::shared_ptr<oxygen::clap::Command>;

} // namespace oxygen::content::pak::tool
