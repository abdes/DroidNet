//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>

namespace oxygen::scripting::bindings {

auto CreateCoreBindingPack() -> contracts::ScriptBindingPackPtr;

} // namespace oxygen::scripting::bindings

// clang-format off
//   - oxygen.time: now(), delta_time(), fixed_delta_time(), frame/tick counters.
//   - oxygen.app: build/platform/runtime info, quit/request-exit, feature flags.
//   - oxygen.log: trace/debug/info/warn/error.
//   - oxygen.math + value types: small engine helpers on top of Luau math (if needed), Vec*/Quat
//     constructors and ops.
//   - oxygen.ids: stable handles/UUID/hash helpers used across domains.
//   - oxygen.events (minimal): generic signal subscription primitives if shared by all packs.
//   - oxygen.debug (dev builds): assertions, profiling scopes, debug toggles.
// clang-format on
