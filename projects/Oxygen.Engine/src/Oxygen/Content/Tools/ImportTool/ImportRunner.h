//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Import/ImportRequest.h>

namespace oxygen::content::import::tool {

[[nodiscard]] auto RunImportJob(
  const ImportRequest& request, bool verbose, bool print_telemetry) -> int;

} // namespace oxygen::content::import::tool
