//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>

struct lua_State;

namespace oxygen::scripting::bindings {

auto RegisterMathBindings(lua_State* state, int oxygen_table_index) -> void;
auto DescribeMathBindings() -> std::span<const BindingNamespaceDescription>;

//! The payload of an `oxygen.quat` userdata.
struct QuatUserdata {
  float x, y, z, w;
};

//! The quaternion at `index`, or null when the value is not an `oxygen.quat`.
auto TryCheckQuat(lua_State* state, int index) -> QuatUserdata*;

//! Pushes `value` as an `oxygen.quat`, the one quaternion every binding pack
//! returns and accepts.
auto PushQuat(lua_State* state, const Quat& value) -> int;

} // namespace oxygen::scripting::bindings
