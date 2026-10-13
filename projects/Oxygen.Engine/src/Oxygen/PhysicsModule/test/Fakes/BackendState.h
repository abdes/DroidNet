//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>
#include <Oxygen/Physics/System/IWorldApi.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>

namespace oxygen::physics::test::detail {

struct BodyState final {
  body::BodyType type { body::BodyType::kStatic };
  Vec3 position { 0.0F };
  Quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  Vec3 linear_velocity { 0.0F };
  Vec3 angular_velocity { 0.0F };
};

struct CharacterState final {
  Vec3 position { 0.0F };
  Quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  Vec3 velocity { 0.0F };
};

struct VehicleState final {
  BodyId chassis_body_id { kInvalidBodyId };
  std::vector<vehicle::VehicleWheelDesc> wheels {};
  vehicle::VehicleControlInput control_input {};
};

struct SoftBodyState final {
  softbody::SoftBodyMaterialParams material_params {};
  softbody::SoftBodyState state {};
};

struct BackendState final {
  WorldId world_id { WorldId { 1U } };
  bool world_created { false };
  bool world_destroyed { false };
  std::size_t step_count { 0 };
  float last_step_dt { 0.0F };
  float last_step_fixed_dt { 0.0F };
  Vec3 gravity { 0.0F, -9.81F, 0.0F };
  std::unordered_map<BodyId, BodyState> bodies {};
  std::unordered_map<CharacterId, CharacterState> characters {};
  std::unordered_map<AggregateId, std::unordered_set<BodyId>> aggregates {};
  std::unordered_map<AggregateId, VehicleState> vehicles {};
  std::unordered_map<AggregateId, SoftBodyState> soft_bodies {};
  std::vector<system::ActiveBodyTransform> active_transforms {};
  std::vector<events::PhysicsEvent> pending_events {};
  BodyId next_body_id { BodyId { 1U } };
  CharacterId next_character_id { CharacterId { 1U } };
  ShapeId next_shape_id { ShapeId { 1U } };
  ShapeInstanceId next_shape_instance_id { ShapeInstanceId { 1U } };
  AreaId next_area_id { AreaId { 1U } };
  JointId next_joint_id { JointId { 1U } };
  AggregateId next_aggregate_id { AggregateId { 1U } };
  std::size_t move_kinematic_calls { 0 };
  std::size_t set_body_pose_calls { 0 };
  std::size_t flush_structural_calls { 0 };
  std::size_t character_create_calls { 0 };
  std::size_t character_destroy_calls { 0 };
  std::size_t character_move_calls { 0 };
  std::size_t aggregate_create_calls { 0 };
  std::size_t aggregate_destroy_calls { 0 };
  std::size_t aggregate_flush_structural_calls { 0 };
  std::size_t articulation_flush_structural_calls { 0 };
  std::size_t vehicle_create_calls { 0 };
  std::size_t vehicle_destroy_calls { 0 };
  std::size_t vehicle_set_control_calls { 0 };
  std::size_t vehicle_flush_structural_calls { 0 };
  std::size_t soft_body_create_calls { 0 };
  std::size_t soft_body_destroy_calls { 0 };
  std::size_t soft_body_set_material_calls { 0 };
  std::size_t soft_body_flush_structural_calls { 0 };
  BodyId last_moved_body { kInvalidBodyId };
  Vec3 last_moved_position { 0.0F };
  Quat last_moved_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
};

} // namespace oxygen::physics::test::detail
