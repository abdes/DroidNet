//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/CollisionFilter.h>

namespace oxygen::physics::world {

struct WorldDesc final {
  Vec3 gravity { oxygen::physics::Gravity };
  std::shared_ptr<ICollisionFilter> collision_filter {};
};

} // namespace oxygen::physics::world
