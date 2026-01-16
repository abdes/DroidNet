//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Clap/Cli.h>

namespace oxygen::content::import::tool {

class ImportCommand;

[[nodiscard]] auto BuildCli(std::span<ImportCommand* const> commands)
  -> std::unique_ptr<clap::Cli>;

} // namespace oxygen::content::import::tool
