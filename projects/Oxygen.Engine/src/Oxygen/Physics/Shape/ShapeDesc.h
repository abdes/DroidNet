//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Physics/Shape.h>

namespace oxygen::physics::shape {

struct ShapeDesc final {
  CollisionShape geometry { SphereShape { 0.5F } };
  Vec3 local_position { 0.0F, 0.0F, 0.0F };
  Quat local_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  Vec3 local_scale { 1.0F, 1.0F, 1.0F };
  bool is_sensor { false };
  uint64_t collision_own_layer { 1ULL };
  uint64_t collision_target_layers { 0xFFFFFFFFFFFFFFFFULL };
};

} // namespace oxygen::physics::shape
