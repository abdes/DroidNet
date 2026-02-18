//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Scripting/api_export.h>

struct lua_State;

namespace oxygen::scripting::bindings {

using NamespaceRegisterFn = void (*)(lua_State* state, int oxygen_table_index);

struct BindingNamespace {
  const char* name { nullptr };
  NamespaceRegisterFn register_fn { nullptr };
};

OXGN_SCRP_NDAPI auto RegisterBindingNamespaces(lua_State* state,
  int runtime_env_ref, std::span<const BindingNamespace> namespaces) -> bool;

} // namespace oxygen::scripting::bindings
