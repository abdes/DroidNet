//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentAsyncBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentLifecycleBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentProceduralBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentQueryBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentUserdataBindings.h>

namespace oxygen::scripting::bindings {

auto RegisterContentBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  RegisterContentUserdataMetatables(state);

  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "assets");
  RegisterContentModuleAvailability(state, module_index);
  RegisterContentModuleQuery(state, module_index);
  RegisterContentModuleAsync(state, module_index);
  RegisterContentModuleLifecycle(state, module_index);
  RegisterContentModuleProcedural(state, module_index);
  RegisterContentModuleLegacyGuards(state, module_index);
  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
