//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include "Oxygen/api_export.h"

namespace oxygen::version {

OXYGEN_API auto Major() -> std::uint8_t;
OXYGEN_API auto Minor() -> std::uint8_t;
OXYGEN_API auto Patch() -> std::uint8_t;

OXYGEN_API auto Version() -> std::string;
OXYGEN_API auto VersionFull() -> std::string;
OXYGEN_API auto NameVersion() -> std::string;

} // namespace oxygen::version
