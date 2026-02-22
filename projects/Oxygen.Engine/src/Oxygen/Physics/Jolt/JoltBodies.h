//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/System/IBodyApi.h>

namespace oxygen::physics::jolt {

class JoltWorld;
class JoltShapes;

//! Jolt implementation of the body domain.
class JoltBodies final : public system::IBodyApi {
public:
  JoltBodies(JoltWorld& world, JoltShapes& shapes);
  ~JoltBodies() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltBodies)
  OXYGEN_MAKE_NON_MOVABLE(JoltBodies)

  auto CreateBody(WorldId world_id, const body::BodyDesc& desc)
    -> PhysicsResult<BodyId> override;
  auto DestroyBody(WorldId world_id, BodyId body_id)
    -> PhysicsResult<void> override;
  auto GetBodyPosition(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override;
  auto GetBodyRotation(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Quat> override;
  auto SetBodyPosition(WorldId world_id, BodyId body_id, const Vec3& position)
    -> PhysicsResult<void> override;
  auto SetBodyRotation(WorldId world_id, BodyId body_id, const Quat& rotation)
    -> PhysicsResult<void> override;
  auto SetBodyPose(WorldId world_id, BodyId body_id, const Vec3& position,
    const Quat& rotation) -> PhysicsResult<void> override;
  auto GetBodyPoses(WorldId world_id, std::span<const BodyId> body_ids,
    std::span<Vec3> out_positions, std::span<Quat> out_rotations) const
    -> PhysicsResult<size_t> override;

  auto GetLinearVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override;
  auto GetAngularVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override;
  auto SetLinearVelocity(WorldId world_id, BodyId body_id, const Vec3& velocity)
    -> PhysicsResult<void> override;
  auto SetAngularVelocity(WorldId world_id, BodyId body_id,
    const Vec3& velocity) -> PhysicsResult<void> override;

  auto AddForce(WorldId world_id, BodyId body_id, const Vec3& force)
    -> PhysicsResult<void> override;
  auto AddImpulse(WorldId world_id, BodyId body_id, const Vec3& impulse)
    -> PhysicsResult<void> override;
  auto AddTorque(WorldId world_id, BodyId body_id, const Vec3& torque)
    -> PhysicsResult<void> override;

  auto MoveKinematic(WorldId world_id, BodyId body_id,
    const Vec3& target_position, const Quat& target_rotation, float delta_time)
    -> PhysicsResult<void> override;
  auto MoveKinematicBatch(WorldId world_id, std::span<const BodyId> body_ids,
    std::span<const Vec3> target_positions,
    std::span<const Quat> target_rotations, float delta_time)
    -> PhysicsResult<size_t> override;
  auto AddBodyShape(WorldId world_id, BodyId body_id, ShapeId shape_id,
    const Vec3& local_position, const Quat& local_rotation)
    -> PhysicsResult<ShapeInstanceId> override;
  auto RemoveBodyShape(WorldId world_id, BodyId body_id,
    ShapeInstanceId shape_instance_id) -> PhysicsResult<void> override;
  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override;

private:
  // Protects body_states_ and shape-instance metadata only.
  // Expensive backend shape rebuilds are performed outside this lock.
  struct ShapeInstanceState final {
    ShapeId shape_id { kInvalidShapeId };
    JPH::RefConst<JPH::Shape> shape {};
    Vec3 local_position { 0.0F, 0.0F, 0.0F };
    Quat local_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  };

  struct BodyState final {
    JPH::RefConst<JPH::Shape> base_shape {};
    std::unordered_map<ShapeInstanceId, ShapeInstanceState> shape_instances {};
  };

  struct BodyKey final {
    WorldId world_id { kInvalidWorldId };
    BodyId body_id { kInvalidBodyId };
    auto operator==(const BodyKey&) const noexcept -> bool = default;
  };

  struct BodyKeyHasher final {
    auto operator()(const BodyKey& key) const noexcept -> size_t
    {
      return std::hash<WorldId> {}(key.world_id)
        ^ (std::hash<BodyId> {}(key.body_id) << 1U);
    }
  };

  auto EnqueueBodyRebuild(WorldId world_id, BodyId body_id) -> void;
  auto RebuildBodyShape(WorldId world_id, BodyId body_id,
    const BodyState& state) -> PhysicsResult<void>;

  observer_ptr<JoltWorld> world_ {};
  observer_ptr<JoltShapes> shapes_ {};
  std::mutex body_state_mutex_ {};
  uint32_t next_shape_instance_id_ { 1U };
  std::unordered_map<BodyKey, BodyState, BodyKeyHasher> body_states_ {};
  std::unordered_set<BodyKey, BodyKeyHasher> pending_rebuilds_ {};
};

} // namespace oxygen::physics::jolt
