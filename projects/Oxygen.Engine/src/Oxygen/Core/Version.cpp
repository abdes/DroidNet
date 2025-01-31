//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Core/Version.h"

#include "Oxygen/version-info.h"

auto oxygen::version::Major() -> std::uint8_t
{
    return info::cVersionMajor;
}

auto oxygen::version::Minor() -> std::uint8_t
{
    return info::cVersionMinor;
}

auto oxygen::version::Patch() -> std::uint8_t
{
    return info::cVersionMajor;
}

auto oxygen::version::Version() -> std::string
{
    return info::cVersion;
}

auto oxygen::version::VersionFull() -> std::string
{
    return std::string(info::cVersion) + " (" + info::cVersionRevision + ")";
}

auto oxygen::version::NameVersion() -> std::string
{
    return info::cNameVersion;
}
