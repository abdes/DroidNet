//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltShapes.h>

auto oxygen::physics::jolt::JoltShapes::CreateShape(
  const shape::ShapeDesc& /*desc*/) -> PhysicsResult<ShapeId>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltShapes::DestroyShape(ShapeId /*shape_id*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}
