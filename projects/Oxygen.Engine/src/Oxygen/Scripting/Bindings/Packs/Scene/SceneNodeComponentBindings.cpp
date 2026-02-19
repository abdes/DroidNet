//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeComponentBindings.h>

namespace oxygen::scripting::bindings {

auto RegisterSceneNodeComponentMethods(
  lua_State* state, const int metatable_index) -> void
{
  RegisterSceneNodeCameraMethods(state, metatable_index);
  RegisterSceneNodeLightMethods(state, metatable_index);
  RegisterSceneNodeRenderableMethods(state, metatable_index);
  RegisterSceneNodeScriptingMethods(state, metatable_index);
}

} // namespace oxygen::scripting::bindings
