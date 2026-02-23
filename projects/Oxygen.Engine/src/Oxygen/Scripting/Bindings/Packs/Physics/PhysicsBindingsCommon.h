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
constexpr const char* kPhysicsAggregateHandleMetatable
  = "oxygen.physics.aggregate_handle";
constexpr const char* kPhysicsAggregateIdMetatable
  = "oxygen.physics.aggregate_id";
constexpr const char* kPhysicsJointHandleMetatable
  = "oxygen.physics.joint_handle";
constexpr const char* kPhysicsJointIdMetatable = "oxygen.physics.joint_id";
constexpr const char* kPhysicsShapeIdMetatable = "oxygen.physics.shape_id";
constexpr const char* kPhysicsShapeInstanceIdMetatable
  = "oxygen.physics.shape_instance_id";

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

struct PhysicsAggregateHandleUserdata final {
  physics::WorldId world_id { physics::kInvalidWorldId };
  physics::AggregateId aggregate_id { physics::kInvalidAggregateId };
};

struct PhysicsAggregateIdUserdata final {
  physics::AggregateId aggregate_id { physics::kInvalidAggregateId };
};

struct PhysicsJointIdUserdata final {
  physics::JointId joint_id { physics::kInvalidJointId };
};

struct PhysicsJointHandleUserdata final {
  physics::WorldId world_id { physics::kInvalidWorldId };
  physics::JointId joint_id { physics::kInvalidJointId };
};

struct PhysicsShapeIdUserdata final {
  physics::ShapeId shape_id { physics::kInvalidShapeId };
};

struct PhysicsShapeInstanceIdUserdata final {
  physics::ShapeInstanceId shape_instance_id {
    physics::kInvalidShapeInstanceId
  };
};

auto RegisterPhysicsBodyHandleMetatable(lua_State* state) -> void;
auto RegisterPhysicsCharacterHandleMetatable(lua_State* state) -> void;
auto RegisterPhysicsBodyIdMetatable(lua_State* state) -> void;
auto RegisterPhysicsCharacterIdMetatable(lua_State* state) -> void;
auto RegisterPhysicsAggregateHandleMetatable(lua_State* state) -> void;
auto RegisterPhysicsAggregateIdMetatable(lua_State* state) -> void;
auto RegisterPhysicsJointHandleMetatable(lua_State* state) -> void;
auto RegisterPhysicsJointIdMetatable(lua_State* state) -> void;
auto RegisterPhysicsShapeIdMetatable(lua_State* state) -> void;
auto RegisterPhysicsShapeInstanceIdMetatable(lua_State* state) -> void;

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

auto PushAggregateHandle(lua_State* state, physics::WorldId world_id,
  physics::AggregateId aggregate_id) -> int;
auto CheckAggregateHandle(lua_State* state, int index)
  -> PhysicsAggregateHandleUserdata*;

auto PushAggregateId(lua_State* state, physics::AggregateId aggregate_id)
  -> int;
auto CheckAggregateId(lua_State* state, int index)
  -> PhysicsAggregateIdUserdata*;

auto PushJointHandle(lua_State* state, physics::WorldId world_id,
  physics::JointId joint_id) -> int;
auto CheckJointHandle(lua_State* state, int index)
  -> PhysicsJointHandleUserdata*;

auto PushJointId(lua_State* state, physics::JointId joint_id) -> int;
auto CheckJointId(lua_State* state, int index) -> PhysicsJointIdUserdata*;

auto PushShapeId(lua_State* state, physics::ShapeId shape_id) -> int;
auto CheckShapeId(lua_State* state, int index) -> PhysicsShapeIdUserdata*;

auto PushShapeInstanceId(
  lua_State* state, physics::ShapeInstanceId shape_instance_id) -> int;
auto CheckShapeInstanceId(lua_State* state, int index)
  -> PhysicsShapeInstanceIdUserdata*;

auto GetPhysicsModule(lua_State* state) -> physics::PhysicsModule*;
auto GetPhysicsWorldId(lua_State* state) -> std::optional<physics::WorldId>;

auto IsPhysicsScriptablePhase(lua_State* state) -> bool;
auto IsAttachAllowed(lua_State* state) -> bool;
auto IsCommandAllowed(lua_State* state) -> bool;
auto IsAggregateMutationAllowed(lua_State* state) -> bool;
auto IsEventDrainAllowed(lua_State* state) -> bool;

OXGN_SCRP_API auto ParseCollisionShape(lua_State* state, int table_index)
  -> physics::CollisionShape;
auto ParseBodyIdArray(lua_State* state, int table_index)
  -> std::vector<physics::BodyId>;
auto ParseBodyIdOrHandle(lua_State* state, int index) -> physics::BodyId;
auto ParseBodyIdOrHandleArray(lua_State* state, int table_index)
  -> std::vector<physics::BodyId>;

} // namespace oxygen::scripting::bindings
