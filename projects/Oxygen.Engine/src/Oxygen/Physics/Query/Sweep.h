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

struct SweepDesc final {
  CollisionShape shape { SphereShape { 0.5F } };
  Vec3 origin { 0.0F, 0.0F, 0.0F };
  Vec3 direction { oxygen::space::move::Forward };
  float max_distance { 1000.0F };
  CollisionMask collision_mask { kCollisionMaskAll };
  std::span<const BodyId> ignore_bodies {};
};

struct SweepHit final {
  BodyId body_id { kInvalidBodyId };
  uint64_t user_data { 0 };
  Vec3 position { 0.0F, 0.0F, 0.0F };
  Vec3 normal { oxygen::space::move::Up };
  float distance { 0.0F };
};

} // namespace oxygen::physics::query
