//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Shape.h>

struct lua_State;

namespace oxygen::physics {
class PhysicsModule;
}

namespace oxygen::scripting::bindings {

constexpr const char* kPhysicsBodyHandleMetatable
  = "oxygen.physics.body_handle";
constexpr const char* kPhysicsCharacterHandleMetatable
  = "oxygen.physics.character_handle";
constexpr const char* kPhysicsBodyIdMetatable = "oxygen.physics.body_id";
constexpr const char* kPhysicsCharacterIdMetatable
  = "oxygen.physics.character_id";

struct PhysicsBodyHandleUserdata final {
  physics::WorldId world_id { physics::kInvalidWorldId };
  physics::BodyId body_id { physics::kInvalidBodyId };
  physics::body::BodyType body_type { physics::body::BodyType::kStatic };
};

struct PhysicsCharacterHandleUserdata final {
  physics::WorldId world_id { physics::kInvalidWorldId };
  physics::CharacterId character_id { physics::kInvalidCharacterId };
};

struct PhysicsBodyIdUserdata final {
  physics::BodyId body_id { physics::kInvalidBodyId };
};

struct PhysicsCharacterIdUserdata final {
  physics::CharacterId character_id { physics::kInvalidCharacterId };
};

auto RegisterPhysicsBodyHandleMetatable(lua_State* state) -> void;
auto RegisterPhysicsCharacterHandleMetatable(lua_State* state) -> void;
auto RegisterPhysicsBodyIdMetatable(lua_State* state) -> void;
auto RegisterPhysicsCharacterIdMetatable(lua_State* state) -> void;

auto PushBodyHandle(lua_State* state, physics::WorldId world_id,
  physics::BodyId body_id, physics::body::BodyType body_type) -> int;
auto CheckBodyHandle(lua_State* state, int index) -> PhysicsBodyHandleUserdata*;

auto PushCharacterHandle(lua_State* state, physics::WorldId world_id,
  physics::CharacterId character_id) -> int;
auto CheckCharacterHandle(lua_State* state, int index)
  -> PhysicsCharacterHandleUserdata*;

auto PushBodyId(lua_State* state, physics::BodyId body_id) -> int;
auto CheckBodyId(lua_State* state, int index) -> PhysicsBodyIdUserdata*;

auto PushCharacterId(lua_State* state, physics::CharacterId character_id)
  -> int;
auto CheckCharacterId(lua_State* state, int index)
  -> PhysicsCharacterIdUserdata*;

auto GetPhysicsModule(lua_State* state) -> physics::PhysicsModule*;
auto GetPhysicsWorldId(lua_State* state) -> std::optional<physics::WorldId>;

auto IsAttachAllowed(lua_State* state) -> bool;
auto IsCommandAllowed(lua_State* state) -> bool;
auto IsEventDrainAllowed(lua_State* state) -> bool;

auto ParseCollisionShape(lua_State* state, int table_index)
  -> physics::CollisionShape;
auto ParseBodyIdArray(lua_State* state, int table_index)
  -> std::vector<physics::BodyId>;

} // namespace oxygen::scripting::bindings
