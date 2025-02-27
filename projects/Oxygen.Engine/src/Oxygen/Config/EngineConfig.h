//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Config/GraphicsConfig.h>

namespace oxygen {

struct EngineConfig {
    std::string application_name; //!< The name of the application.
    uint32_t application_version { 0 }; //!< The version of the application.

    GraphicsConfig graphics; //!< Graphics configuration.
};

} // namespace oxygen
