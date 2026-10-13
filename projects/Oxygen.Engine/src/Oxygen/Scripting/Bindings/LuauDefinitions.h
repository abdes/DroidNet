//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <string>
#include <vector>

#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting::bindings {

//! The binding packs the scripting module registers by default, in
//! registration order.
OXGN_SCRP_NDAPI auto CreateDefaultBindingPacks()
  -> std::vector<contracts::ScriptBindingPackPtr>;

//! Writes Luau definitions (`oxygen.d.luau`) for the `oxygen` API.
/*!
 Built from the packs' namespace descriptions only, so the definitions
 cannot drift from the registrations: type aliases, then one extern type per
 userdata class, then `declare oxygen` with every namespace. A namespace
 without a description is declared `any`.

 Descriptions are merged by name: classes described in several places (the
 scene node and its component methods) become one extern type. A type alias
 or member declared twice with different types is a programming error.
*/
OXGN_SCRP_NDAPI auto GenerateLuauDefinitions(
  std::span<const contracts::ScriptBindingPackPtr> packs) -> std::string;

} // namespace oxygen::scripting::bindings
