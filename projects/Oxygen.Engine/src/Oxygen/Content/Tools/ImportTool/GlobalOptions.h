//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/CliTheme.h>

namespace oxygen::content::import::tool {

class IMessageWriter; // forward-declared
} // namespace oxygen::content::import::tool

namespace oxygen::content::import {
class AsyncImportService; // forward-declared
} // namespace oxygen::content::import

namespace oxygen::content::import::tool {

struct GlobalOptions {
  bool quiet = false;
  std::string diagnostics_file;
  std::string cooked_root;
  std::string command_line;
  bool fail_fast = false;
  bool no_color = false;
  bool no_tui = false;
  clap::CliThemeKind theme = clap::CliThemeKind::kDark;

  // Injected message writer (owned by main). Main creates concrete writers
  // and stores non-owning observer_ptrs here. The writer MUST be provided by
  // main and MUST NOT be recreated by clients.
  oxygen::observer_ptr<IMessageWriter> writer;
  oxygen::observer_ptr<AsyncImportService> import_service;
};

} // namespace oxygen::content::import::tool
