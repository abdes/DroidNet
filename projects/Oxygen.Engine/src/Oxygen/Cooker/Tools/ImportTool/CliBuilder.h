//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>

namespace oxygen::content::import::tool {

//! The option groups every import tool command accepts, before its name.
/*!
 The global options parse into `global_options`; the service options are read
 from the parsed values when a command's import service is configured.
*/
[[nodiscard]] auto BuildGlobalOptionGroups(GlobalOptions& global_options)
  -> std::vector<std::shared_ptr<clap::Options>>;

} // namespace oxygen::content::import::tool
