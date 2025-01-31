//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include "Oxygen/Base/api_export.h"
#include "Oxygen/version-info.h"

namespace oxygen {

OXYGEN_BASE_API std::string VersionInfo();

// We need at least one exported symbol if MSVC is to generate a .lib file.
// This is a dummy function that does nothing, but it is exported.
std::string VersionInfo()
{
    return oxygen::info::cNameVersion;
}

} // namespace oxygen
