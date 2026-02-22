//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/System/IWorldApi.h>

namespace JPH {
class BodyInterface;
class BodyLockInterface;
class NarrowPhaseQuery;
}

namespace oxygen::physics::jolt {

//! Jolt implementation of the world domain.
class JoltWorld final : public system::IWorldApi {
public:
  JoltWorld();
  ~JoltWorld() override;

  OXYGEN_MAKE_NON_COPYABLE(JoltWorld)
  OXYGEN_MAKE_NON_MOVABLE(JoltWorld)

  auto CreateWorld(const world::WorldDesc& desc)
    -> PhysicsResult<WorldId> override;
  auto DestroyWorld(WorldId world_id) -> PhysicsResult<void> override;
  auto Step(WorldId world_id, float delta_time, int max_sub_steps,
    float fixed_dt_seconds) -> PhysicsResult<void> override;

  auto GetActiveBodyTransforms(WorldId world_id,
    std::span<system::ActiveBodyTransform> out_transforms) const
    -> PhysicsResult<size_t> override;

  auto GetGravity(WorldId world_id) const -> PhysicsResult<Vec3> override;
  auto SetGravity(WorldId world_id, const Vec3& gravity)
    -> PhysicsResult<void> override;

  [[nodiscard]] auto TryGetBodyInterface(WorldId world_id) noexcept
    -> observer_ptr<JPH::BodyInterface>;
  [[nodiscard]] auto TryGetBodyInterface(WorldId world_id) const noexcept
    -> observer_ptr<const JPH::BodyInterface>;
  [[nodiscard]] auto TryGetBodyLockInterface(WorldId world_id) const noexcept
    -> observer_ptr<const JPH::BodyLockInterface>;
  [[nodiscard]] auto TryGetNarrowPhaseQuery(WorldId world_id) const noexcept
    -> observer_ptr<const JPH::NarrowPhaseQuery>;

  [[nodiscard]] auto HasBody(WorldId world_id, BodyId body_id) const noexcept
    -> bool;
  auto RegisterBody(WorldId world_id, BodyId body_id) -> PhysicsResult<void>;
  auto UnregisterBody(WorldId world_id, BodyId body_id) -> PhysicsResult<void>;

private:
  struct WorldState;

  [[nodiscard]] auto TryGetWorld(WorldId world_id) noexcept
    -> observer_ptr<WorldState>;
  [[nodiscard]] auto TryGetWorld(WorldId world_id) const noexcept
    -> observer_ptr<const WorldState>;

  bool runtime_ready_ { false };
  uint32_t next_world_id_ { 1U };
  std::unordered_map<WorldId, std::unique_ptr<WorldState>> worlds_ {};
};

} // namespace oxygen::physics::jolt
