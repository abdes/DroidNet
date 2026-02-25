//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scripting/api_export.h>

struct lua_State;
namespace oxygen::engine {
class FrameContext;
}

namespace oxygen {
class IAsyncEngine;
}

namespace oxygen::scripting::bindings {

//=== Luau Userdata Lifecycle ===--------------------------------------------===
//
// Luau (unlike standard Lua 5.x) does **not** support the `__gc`
// metamethod.  Setting `__gc` on a metatable has no effect; the VM
// will never call it.  The *only* mechanism for release of C++
// resources attached to userdata is:
//
//   1. Allocate with `lua_newuserdatatagged(L, size, tag)`.
//   2. Register a destructor once per tag with
//      `lua_setuserdatadtor(L, tag, DtorFn)`.
//
// The destructor signature is `void(lua_State*, void*)` — **not**
// `int(lua_State*)`.
//
// ### DO
//
// - Assign every non-trivially-destructible userdata type its own
//   tag in `LuauUserdataTag`.  A single tag may be shared across
//   types only when they are layout-compatible and share the same
//   destructor (e.g. all `AssetUserdata` variants use `kTagAsset`).
// - Call `lua_setuserdatadtor` exactly once per tag, typically in the
//   `Register*Metatable()` function, **before** any userdata of
//   that tag is allocated.
// - Write the destructor as a plain call to the placement
//   destructor:
//
//       void MyTypeDtor(lua_State* \/\*state\*\/, void* data)
//       {
//           static_cast<MyType*>(data)->~MyType();
//       }
//
// - Use `std::construct_at` or placement new to initialise the
//   memory returned by `lua_newuserdatatagged`.
// - Trivially-destructible types (POD floats, raw pointers,
//   `observer_ptr`, integer handles) may use untagged
//   `lua_newuserdata` — no destructor is required.
//
// ### DO NOT
//
// - **Never** register a `__gc` metamethod.  It is silently ignored
//   by Luau and will leak every C++ object the userdata owns.
// - **Never** use untagged `lua_newuserdata` for types that contain
//   `shared_ptr`, `weak_ptr`, `unique_ptr`, `std::string`,
//   `std::vector`, or any other member with a non-trivial
//   destructor.  The Luau GC will free the raw memory without
//   calling any C++ destructor, causing resource and memory leaks.
// - **Never** use `lua_touserdata` to retrieve tagged userdata
//   without verifying the tag via `lua_userdatatag`.
//
// ### Correct example (non-trivial type)
//
//   ```cpp
//   void MyDtor(lua_State* \/\*state\*\/, void* data) {
//       static_cast<MyUserdata*>(data)->~MyUserdata();
//   }
//
//   void RegisterMyMetatable(lua_State* state) {
//       luaL_newmetatable(state, kMyMetatable);
//       lua_setuserdatadtor(state, kTagMy, MyDtor);
//       \/\/ ... register __index, __tostring, etc. ...
//       lua_pop(state, 1);
//   }
//
//   int PushMyUserdata(lua_State* state, MyPayload payload) {
//       void* mem = lua_newuserdatatagged(
//           state, sizeof(MyUserdata), kTagMy);
//       new (mem) MyUserdata { std::move(payload) };
//       luaL_getmetatable(state, kMyMetatable);
//       lua_setmetatable(state, -2);
//       return 1;
//   }
//   ```
//
// @see lua_newuserdatatagged, lua_setuserdatadtor (Luau C API)
//===------------------------------------------------------------------------===

//! Luau userdata tags for Oxygen types.
/*!
 Each tag identifies a distinct userdata layout so the Luau GC can
 invoke the correct C++ destructor registered via
 `lua_setuserdatadtor`.  Luau supports up to 128 tags (0-127).

 Tags are grouped by subsystem with gaps left for future additions.

 @warning Every non-trivially-destructible userdata **must** have a
          tag.  See the "Luau Userdata Lifecycle" comment block above
          for the full pattern.
*/
enum LuauUserdataTag : uint8_t {
  kTagBindingContext = 10,
  kTagRuntimeContext = 11,
  kTagEventRuntime = 12,
  kTagVec4 = 20,
  kTagQuat = 21,
  kTagMat4 = 22,
  kTagSceneNode = 30,
  kTagSceneQuery = 31,
  kTagTextureResource = 40,
  kTagBufferResource = 41,
  kTagAsset = 42,
};

struct LuaSlotExecutionContext {
  scene::NodeHandle node_handle;
  const scene::ScriptingComponent::Slot* slot { nullptr };
};

struct LuaBindingContext {
  LuaSlotExecutionContext slot_context {};
  bool has_slot_context { false };
  float dt_seconds { 0.0F };
};

OXGN_SCRP_API auto PushScriptContext(lua_State* state,
  LuaSlotExecutionContext* slot_context, float dt_seconds) -> void;

OXGN_SCRP_API auto SetActiveFrameContext(lua_State* state,
  observer_ptr<engine::FrameContext> frame_context) noexcept -> void;

OXGN_SCRP_NDAPI auto GetActiveFrameContext(lua_State* state) noexcept
  -> observer_ptr<engine::FrameContext>;

OXGN_SCRP_API auto SetActiveEngine(
  lua_State* state, observer_ptr<IAsyncEngine> engine) noexcept -> void;

OXGN_SCRP_NDAPI auto GetActiveEngine(lua_State* state) noexcept
  -> observer_ptr<IAsyncEngine>;

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
