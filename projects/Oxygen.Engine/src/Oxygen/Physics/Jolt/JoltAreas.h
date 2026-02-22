//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/System/IAreaApi.h>

namespace oxygen::physics::jolt {

class JoltWorld;
class JoltShapes;

//! Jolt implementation of the area domain.
class JoltAreas final : public system::IAreaApi {
public:
  JoltAreas(JoltWorld& world, JoltShapes& shapes);
  ~JoltAreas() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltAreas)
  OXYGEN_MAKE_NON_MOVABLE(JoltAreas)

  auto CreateArea(WorldId world_id, const area::AreaDesc& desc)
    -> PhysicsResult<AreaId> override;
  auto DestroyArea(WorldId world_id, AreaId area_id)
    -> PhysicsResult<void> override;

  auto GetAreaPosition(WorldId world_id, AreaId area_id) const
    -> PhysicsResult<Vec3> override;
  auto GetAreaRotation(WorldId world_id, AreaId area_id) const
    -> PhysicsResult<Quat> override;
  auto SetAreaPose(WorldId world_id, AreaId area_id, const Vec3& position,
    const Quat& rotation) -> PhysicsResult<void> override;

  auto AddAreaShape(WorldId world_id, AreaId area_id, ShapeId shape_id,
    const Vec3& local_position, const Quat& local_rotation)
    -> PhysicsResult<ShapeInstanceId> override;
  auto RemoveAreaShape(WorldId world_id, AreaId area_id,
    ShapeInstanceId shape_instance_id) -> PhysicsResult<void> override;

private:
  struct ShapeInstanceState final {
    ShapeId shape_id { kInvalidShapeId };
    JPH::RefConst<JPH::Shape> shape {};
    Vec3 local_position { 0.0F, 0.0F, 0.0F };
    Quat local_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  };

  struct AreaState final {
    JPH::BodyID jolt_body_id {};
    std::unordered_map<ShapeInstanceId, ShapeInstanceState> shape_instances {};
  };

  struct AreaKey final {
    WorldId world_id { kInvalidWorldId };
    AreaId area_id { kInvalidAreaId };
    auto operator==(const AreaKey&) const noexcept -> bool = default;
  };

  struct AreaKeyHasher final {
    auto operator()(const AreaKey& key) const noexcept -> size_t
    {
      return std::hash<WorldId> {}(key.world_id)
        ^ (std::hash<AreaId> {}(key.area_id) << 1U);
    }
  };

  auto RebuildAreaShape(const AreaState& state)
    -> PhysicsResult<JPH::RefConst<JPH::Shape>>;

  observer_ptr<JoltWorld> world_ {};
  observer_ptr<JoltShapes> shapes_ {};
  mutable std::mutex mutex_ {};
  uint32_t next_area_id_ { 1U };
  uint32_t next_shape_instance_id_ { 1U };
  std::unordered_map<AreaKey, AreaState, AreaKeyHasher> areas_ {};
};

} // namespace oxygen::physics::jolt
