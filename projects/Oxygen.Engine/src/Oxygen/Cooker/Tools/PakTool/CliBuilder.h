//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Clap/Cli.h>

namespace oxygen::content::pak::tool {

[[nodiscard]] auto BuildCli() -> std::unique_ptr<oxygen::clap::Cli>;

} // namespace oxygen::content::pak::tool
