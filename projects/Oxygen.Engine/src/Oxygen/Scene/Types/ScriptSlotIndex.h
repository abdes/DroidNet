//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::scene {

//! Stable script slot index in a ScriptingComponent slot array.
using ScriptSlotIndex
  = oxygen::NamedType<uint32_t, struct ScriptSlotIndexTag, oxygen::Comparable,
    oxygen::Hashable, oxygen::FunctionCallable, oxygen::DefaultInitialized>;

[[nodiscard]] inline auto to_string(const ScriptSlotIndex index) -> std::string
{
  return "ScriptSlotIndex{" + std::to_string(index.get()) + "}";
}

} // namespace oxygen::scene
