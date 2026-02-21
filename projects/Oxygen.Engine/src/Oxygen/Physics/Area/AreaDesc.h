//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/CollisionLayers.h>

namespace oxygen::physics::area {

struct AreaDesc final {
  Vec3 initial_position { 0.0F, 0.0F, 0.0F };
  Quat initial_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  CollisionLayer collision_layer { kCollisionLayerDefault };
  CollisionMask collision_mask { kCollisionMaskAll };
};

} // namespace oxygen::physics::area
