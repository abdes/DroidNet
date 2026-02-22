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
#include <Oxygen/Physics/System/IShapeApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the shape domain.
class JoltShapes final : public system::IShapeApi {
public:
  JoltShapes() = default;
  ~JoltShapes() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltShapes)
  OXYGEN_MAKE_NON_MOVABLE(JoltShapes)

  auto CreateShape(const shape::ShapeDesc& desc)
    -> PhysicsResult<ShapeId> override;
  auto DestroyShape(ShapeId shape_id) -> PhysicsResult<void> override;

  [[nodiscard]] auto TryGetShape(ShapeId shape_id) const
    -> PhysicsResult<JPH::RefConst<JPH::Shape>>;
  auto AddAttachment(ShapeId shape_id) -> PhysicsResult<void>;
  auto RemoveAttachment(ShapeId shape_id) -> PhysicsResult<void>;

private:
  struct ShapeState final {
    JPH::RefConst<JPH::Shape> shape {};
    size_t attachment_count { 0U };
  };

  mutable std::mutex mutex_ {};
  uint32_t next_shape_id_ { 1U };
  std::unordered_map<ShapeId, ShapeState> shapes_ {};
};

} // namespace oxygen::physics::jolt
