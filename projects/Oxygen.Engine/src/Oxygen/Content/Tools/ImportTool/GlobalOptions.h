//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Clap/CliTheme.h>

namespace oxygen::content::import::tool {

struct GlobalOptions {
  bool quiet = false;
  std::string diagnostics_file;
  std::string cooked_root;
  bool fail_fast = false;
  bool no_color = false;
  bool no_tui = false;
  clap::CliThemeKind theme = clap::CliThemeKind::kDark;
};

auto ApplyLoggingOptions(const GlobalOptions& options) -> void;

} // namespace oxygen::content::import::tool
