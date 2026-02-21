//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
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
};

} // namespace oxygen::physics::jolt
