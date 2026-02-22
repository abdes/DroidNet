//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>
#include <variant>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/Shape.h>

namespace oxygen::physics::jolt {

inline auto ToJoltBodyId(const BodyId body_id) -> JPH::BodyID
{
  return JPH::BodyID { body_id.get() };
}

inline auto ToJoltVec3(const Vec3& value) -> JPH::Vec3
{
  return JPH::Vec3 { value.x, value.y, value.z };
}

inline auto ToJoltRVec3(const Vec3& value) -> JPH::RVec3
{
  return JPH::RVec3 { value.x, value.y, value.z };
}

inline auto ToJoltQuat(const Quat& value) -> JPH::Quat
{
  return JPH::Quat { value.x, value.y, value.z, value.w };
}

template <typename TVec> inline auto ToOxygenVec3(const TVec& value) -> Vec3
{
  return Vec3 { static_cast<float>(value.GetX()),
    static_cast<float>(value.GetY()), static_cast<float>(value.GetZ()) };
}

inline auto ToOxygenQuat(const JPH::Quat& value) -> Quat
{
  return Quat {
    value.GetW(),
    value.GetX(),
    value.GetY(),
    value.GetZ(),
  };
}

inline auto ToMotionType(const body::BodyType type) -> JPH::EMotionType
{
  switch (type) {
  case body::BodyType::kStatic:
    return JPH::EMotionType::Static;
  case body::BodyType::kDynamic:
    return JPH::EMotionType::Dynamic;
  case body::BodyType::kKinematic:
    return JPH::EMotionType::Kinematic;
  }
  return JPH::EMotionType::Static;
}

inline auto ToObjectLayer(const body::BodyType type) -> JPH::ObjectLayer
{
  return type == body::BodyType::kStatic ? 0 : 1;
}

inline auto ToActivation(const body::BodyType type) -> JPH::EActivation
{
  return type == body::BodyType::kDynamic ? JPH::EActivation::Activate
                                          : JPH::EActivation::DontActivate;
}

inline auto MakeShape(const CollisionShape& shape)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  return std::visit(
    [](const auto& value) -> PhysicsResult<JPH::RefConst<JPH::Shape>> {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, SphereShape>) {
        if (value.radius <= 0.0F) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(
          JPH::RefConst<JPH::Shape> { new JPH::SphereShape(value.radius) });
      } else if constexpr (std::is_same_v<T, BoxShape>) {
        if (value.extents.x <= 0.0F || value.extents.y <= 0.0F
          || value.extents.z <= 0.0F) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(JPH::RefConst<JPH::Shape> {
          new JPH::BoxShape(ToJoltVec3(value.extents)),
        });
      } else if constexpr (std::is_same_v<T, CapsuleShape>) {
        if (value.radius <= 0.0F || value.half_height <= 0.0F) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(JPH::RefConst<JPH::Shape> {
          new JPH::CapsuleShape(value.half_height, value.radius),
        });
      } else if constexpr (std::is_same_v<T, MeshShape>) {
        return Err(PhysicsError::kNotImplemented);
      } else {
        return Err(PhysicsError::kNotImplemented);
      }
    },
    shape);
}

} // namespace oxygen::physics::jolt
