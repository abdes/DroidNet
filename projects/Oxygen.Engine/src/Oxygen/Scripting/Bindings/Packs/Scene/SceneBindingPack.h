//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>

namespace oxygen::scripting::bindings {

auto CreateSceneBindingPack() -> contracts::ScriptBindingPackPtr;

} // namespace oxygen::scripting::bindings
