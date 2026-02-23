//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsEventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto EventQueueName(const physics::events::PhysicsEventType type) -> const
    char*
  {
    switch (type) {
    case physics::events::PhysicsEventType::kContactBegin:
      return "physics.contact_begin";
    case physics::events::PhysicsEventType::kContactEnd:
      return "physics.contact_end";
    case physics::events::PhysicsEventType::kTriggerBegin:
      return "physics.trigger_begin";
    case physics::events::PhysicsEventType::kTriggerEnd:
      return "physics.trigger_end";
    }
    return "physics.contact_begin";
  }

  auto PushOptionalLiveNode(lua_State* state,
    const std::optional<scene::NodeHandle>& node_handle) -> int
  {
    if (!node_handle.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto frame_context = GetActiveFrameContext(state);
    if (frame_context == nullptr || frame_context->GetScene() == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto node = frame_context->GetScene()->GetNode(*node_handle);
    if (!node.has_value() || !node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneNode(state, *node);
  }

  auto PushPhysicsEventTable(lua_State* state,
    const physics::PhysicsModule::ScenePhysicsEvent& event) -> int
  {
    lua_createtable(state, 0, 9);

    const auto type_sv = physics::events::to_string(event.type);
    lua_pushlstring(state, type_sv.data(), type_sv.size());
    lua_setfield(state, -2, "type");

    PushOptionalLiveNode(state, event.node_a);
    lua_setfield(state, -2, "node_a");

    PushOptionalLiveNode(state, event.node_b);
    lua_setfield(state, -2, "node_b");

    PushBodyId(state, event.raw_event.body_a);
    lua_setfield(state, -2, "body_a");

    PushBodyId(state, event.raw_event.body_b);
    lua_setfield(state, -2, "body_b");

    PushVec3(state, event.raw_event.contact_normal);
    lua_setfield(state, -2, "contact_normal");

    PushVec3(state, event.raw_event.contact_position);
    lua_setfield(state, -2, "contact_position");

    lua_pushnumber(state, event.raw_event.penetration_depth);
    lua_setfield(state, -2, "penetration_depth");

    PushVec3(state, event.raw_event.applied_impulse);
    lua_setfield(state, -2, "applied_impulse");

    return 1;
  }

  auto LuaPhysicsEventsDrain(lua_State* state) -> int
  {
    if (!IsEventDrainAllowed(state)) {
      lua_createtable(state, 0, 0);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_createtable(state, 0, 0);
      return 1;
    }

    const auto events = physics_module->ConsumeSceneEvents();
    lua_createtable(state, static_cast<int>(events.size()), 0);
    int lua_index = 1;
    for (const auto& event : events) {
      static_cast<void>(PushPhysicsEventTable(state, event));
      QueueEngineEventWithPayload(
        state, EventQueueName(event.type), "scene_mutation", -1);
      lua_rawseti(state, -2, lua_index);
      ++lua_index;
    }
    return 1;
  }

} // namespace

auto RegisterPhysicsEventsBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto events_index = PushOxygenSubtable(state, physics_index, "events");

  lua_pushcfunction(state, LuaPhysicsEventsDrain, "physics.events.drain");
  lua_setfield(state, events_index, "drain");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
