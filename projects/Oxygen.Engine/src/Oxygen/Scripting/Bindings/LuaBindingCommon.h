//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scripting/api_export.h>

struct lua_State;
namespace oxygen::engine {
class FrameContext;
}
namespace oxygen {
class AsyncEngine;
}

namespace oxygen::scripting::bindings {

struct LuaSlotExecutionContext {
  scene::SceneNode node;
  const scene::ScriptingComponent::Slot* slot { nullptr };
};

struct LuaBindingContext {
  LuaSlotExecutionContext* slot_context { nullptr };
  float dt_seconds { 0.0F };
};

OXGN_SCRP_API auto PushScriptContext(lua_State* state,
  LuaSlotExecutionContext* slot_context, float dt_seconds) -> void;

OXGN_SCRP_API auto SetActiveFrameContext(lua_State* state,
  observer_ptr<engine::FrameContext> frame_context) noexcept -> void;

OXGN_SCRP_NDAPI auto GetActiveFrameContext(lua_State* state) noexcept
  -> observer_ptr<engine::FrameContext>;

OXGN_SCRP_API auto SetActiveEngine(
  lua_State* state, observer_ptr<AsyncEngine> engine) noexcept -> void;

OXGN_SCRP_NDAPI auto GetActiveEngine(lua_State* state) noexcept
  -> observer_ptr<AsyncEngine>;

OXGN_SCRP_NDAPI auto GetBindingContextFromScriptArg(
  lua_State* state, int arg_index = 1) noexcept -> LuaBindingContext*;

OXGN_SCRP_NDAPI auto PushScriptParam(
  lua_State* state, const data::ScriptParam& param) -> int;

OXGN_SCRP_NDAPI auto GetParamValue(lua_State* state) -> int;

OXGN_SCRP_NDAPI auto SetLocalRotationEuler(
  lua_State* state, float x, float y, float z) -> int;

OXGN_SCRP_NDAPI auto SetLocalRotationQuat(
  lua_State* state, float x, float y, float z, float w) -> int;

OXGN_SCRP_NDAPI auto PushOxygenSubtable(
  lua_State* state, int oxygen_table_index, const char* field_name) -> int;

} // namespace oxygen::scripting::bindings
