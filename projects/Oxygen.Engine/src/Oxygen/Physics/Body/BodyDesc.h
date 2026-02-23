//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <type_traits> // IWYU pragma: keep

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/CollisionLayers.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics::body {

enum class BodyType : uint8_t {
  kStatic = 0,
  kDynamic,
  kKinematic,
};

OXGN_PHYS_NDAPI auto to_string(BodyType value) noexcept -> const char*;

enum class BodyFlags : uint32_t {
  kNone = 0,
  kEnableGravity = OXYGEN_FLAG(0),
  kIsTrigger = OXYGEN_FLAG(1),
  kEnableContinuousCollisionDetection = OXYGEN_FLAG(2),
  kAll = kEnableGravity | kIsTrigger | kEnableContinuousCollisionDetection,
};
OXYGEN_DEFINE_FLAGS_OPERATORS(BodyFlags)

OXGN_PHYS_NDAPI auto to_string(BodyFlags value) -> std::string;

struct BodyDesc final {
  BodyType type { BodyType::kStatic };
  BodyFlags flags { BodyFlags::kEnableGravity };
  /*! Convenience initial shape.
   This creates one initial shape instance on
   * body creation. Additional shapes
   can be attached via
   * `IBodyApi::AddBodyShape`.
  */
  CollisionShape shape { SphereShape { 0.5F } };
  Vec3 shape_local_position { 0.0F, 0.0F, 0.0F };
  Quat shape_local_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  Vec3 shape_local_scale { 1.0F, 1.0F, 1.0F };
  Vec3 initial_position { 0.0F, 0.0F, 0.0F };
  Quat initial_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  float mass_kg { 1.0F };
  float linear_damping { 0.05F };
  float angular_damping { 0.05F };
  float gravity_factor { 1.0F };
  float friction { 0.5F };
  float restitution { 0.0F };
  CollisionLayer collision_layer { kCollisionLayerDefault };
  CollisionMask collision_mask { kCollisionMaskAll };
};

} // namespace oxygen::physics::body
