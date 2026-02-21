//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/CollisionLayers.h>
#include <Oxygen/Physics/Handles.h>

namespace oxygen::physics::query {

struct RaycastDesc final {
  Vec3 origin { 0.0F, 0.0F, 0.0F };
  Vec3 direction { oxygen::space::move::Forward };
  float max_distance { 1000.0F };
  CollisionMask collision_mask { kCollisionMaskAll };
  std::span<const BodyId> ignore_bodies {};
};

struct RaycastHit final {
  BodyId body_id { kInvalidBodyId };
  uint64_t user_data { 0 };
  Vec3 position { 0.0F, 0.0F, 0.0F };
  Vec3 normal { oxygen::space::move::Up };
  float distance { 0.0F };
};

using OptionalRaycastHit = std::optional<RaycastHit>;

} // namespace oxygen::physics::query
