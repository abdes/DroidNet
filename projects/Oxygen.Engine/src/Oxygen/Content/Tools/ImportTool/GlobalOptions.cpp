//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>

namespace oxygen::content::import::tool {

auto ApplyLoggingOptions(const GlobalOptions& options) -> void
{
  loguru::g_colorlogtostderr = !options.no_color;
}

} // namespace oxygen::content::import::tool
