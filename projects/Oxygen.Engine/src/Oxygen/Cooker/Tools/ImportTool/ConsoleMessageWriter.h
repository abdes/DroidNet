//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>

namespace oxygen::content::import::tool {

//! The writer the run's global options select.
/*!
 With `--no-tui`, a console writer that animates progress on an interactive
 terminal; otherwise a muted writer that leaves the screen to the TUI and
 prints only errors and warnings. Both write to `streams`.
*/
[[nodiscard]] auto CreateMessageWriter(const GlobalOptions& global_options,
  toolkit::ToolStreams streams) -> std::unique_ptr<IMessageWriter>;

} // namespace oxygen::content::import::tool
