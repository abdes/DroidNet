//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

#include "DemoShell/Runtime/MainViewContract.h"

namespace oxygen::engine {
using ShaderDebugMode = vortex::ShaderDebugMode;
using vortex::IsIblDebugMode;
using vortex::IsLightCullingDebugMode;
using vortex::IsNonIblDebugMode;
} // namespace oxygen::engine
