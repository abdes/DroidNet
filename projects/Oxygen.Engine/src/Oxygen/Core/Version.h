//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Core/api_export.h>

namespace oxygen::version {

OXGN_CORE_NDAPI auto Major() -> std::uint8_t;
OXGN_CORE_NDAPI auto Minor() -> std::uint8_t;
OXGN_CORE_NDAPI auto Patch() -> std::uint8_t;

OXGN_CORE_NDAPI auto Version() -> std::string;
OXGN_CORE_NDAPI auto VersionFull() -> std::string;
OXGN_CORE_NDAPI auto NameVersion() -> std::string;

} // namespace oxygen::version
