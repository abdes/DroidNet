//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/CollisionLayers.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics::query {

struct OverlapDesc final {
  CollisionShape shape { SphereShape { 0.5F } };
  Vec3 center { 0.0F, 0.0F, 0.0F };
  CollisionMask collision_mask { kCollisionMaskAll };
  std::span<const BodyId> ignore_bodies {};
};

} // namespace oxygen::physics::query
