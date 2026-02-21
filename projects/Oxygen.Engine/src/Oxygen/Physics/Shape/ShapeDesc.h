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
};

} // namespace oxygen::physics::shape
