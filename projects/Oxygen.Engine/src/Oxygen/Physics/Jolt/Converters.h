//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/DecoratedShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>

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
  // Legacy fallback mapping used by non-authored domains (e.g. characters).
  // Authored rigid-body layer/mask routing is resolved by JoltWorld.
  return type == body::BodyType::kStatic ? 0 : 1;
}

inline auto ToActivation(const body::BodyType type) -> JPH::EActivation
{
  return type == body::BodyType::kDynamic ? JPH::EActivation::Activate
                                          : JPH::EActivation::DontActivate;
}

inline auto RestoreCookedJoltShape(const CookedShapePayload& payload)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  if (payload.data.empty()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const std::string blob(
    reinterpret_cast<const char*>(payload.data.data()), payload.data.size());
  std::istringstream stream(blob, std::ios::binary);
  JPH::StreamInWrapper wrapped(stream);
  auto restored = JPH::Shape::sRestoreFromBinaryState(wrapped);
  if (!restored.IsValid()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  return Ok(JPH::RefConst<JPH::Shape>(restored.Get()));
}

inline auto ValidateCompoundChildScaleContract(const JPH::Shape& shape) -> bool
{
  if (shape.GetType() == JPH::EShapeType::Compound) {
    const auto& compound = static_cast<const JPH::CompoundShape&>(shape);
    for (uint32_t i = 0; i < compound.GetNumSubShapes(); ++i) {
      const auto& child = compound.GetSubShape(i);
      if (child.mShape == nullptr
        || !ValidateCompoundChildScaleContract(*child.mShape)) {
        return false;
      }
    }
    return true;
  }

  if (shape.GetType() == JPH::EShapeType::Decorated
    && shape.GetSubType() == JPH::EShapeSubType::Scaled) {
    const auto& scaled = static_cast<const JPH::ScaledShape&>(shape);
    const auto* inner = scaled.GetInnerShape();
    if (inner == nullptr) {
      return false;
    }
    if (!inner->IsValidScale(scaled.GetScale())) {
      return false;
    }
    return ValidateCompoundChildScaleContract(*inner);
  }

  if (shape.GetType() == JPH::EShapeType::Decorated) {
    const auto& decorated = static_cast<const JPH::DecoratedShape&>(shape);
    const auto* inner = decorated.GetInnerShape();
    return inner != nullptr && ValidateCompoundChildScaleContract(*inner);
  }

  return true;
}

inline auto MakeWorldBoundaryShape(const WorldBoundaryShape& value)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  const auto min = value.limits_min;
  const auto max = value.limits_max;
  if (!(std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(min.z)
        && std::isfinite(max.x) && std::isfinite(max.y)
        && std::isfinite(max.z))) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (max.x <= min.x || max.y <= min.y || max.z <= min.z) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (value.mode != WorldBoundaryMode::kAabbClamp
    && value.mode != WorldBoundaryMode::kPlaneSet) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const Vec3 half_extents = (max - min) * 0.5F;
  return Ok(JPH::RefConst<JPH::Shape> {
    new JPH::BoxShape(ToJoltVec3(half_extents)),
  });
}

inline auto IsNearlyEqual(const float lhs, const float rhs) noexcept -> bool
{
  return std::abs(lhs - rhs) <= 1e-5F;
}

inline auto IsFiniteTranslation(const Vec3& value) noexcept -> bool
{
  return std::isfinite(value.x) && std::isfinite(value.y)
    && std::isfinite(value.z);
}

inline auto IsFiniteRotation(const Quat& value) noexcept -> bool
{
  return std::isfinite(value.w) && std::isfinite(value.x)
    && std::isfinite(value.y) && std::isfinite(value.z);
}

inline auto RotationNormSquared(const Quat& value) noexcept -> float
{
  return value.w * value.w + value.x * value.x + value.y * value.y
    + value.z * value.z;
}

inline auto IsValidRotation(const Quat& value) noexcept -> bool
{
  const auto norm2 = RotationNormSquared(value);
  return IsFiniteRotation(value) && std::isfinite(norm2) && norm2 > 1e-8F;
}

inline auto NormalizeRotation(const Quat& value) noexcept -> Quat
{
  const auto norm2 = RotationNormSquared(value);
  const auto inv_norm = 1.0F / std::sqrt(norm2);
  return Quat {
    value.w * inv_norm,
    value.x * inv_norm,
    value.y * inv_norm,
    value.z * inv_norm,
  };
}

inline auto IsIdentityRotation(const Quat& value) noexcept -> bool
{
  const Quat identity { 1.0F, 0.0F, 0.0F, 0.0F };
  const Quat neg_identity { -1.0F, 0.0F, 0.0F, 0.0F };
  const auto near = [](const Quat& a, const Quat& b) {
    return IsNearlyEqual(a.w, b.w) && IsNearlyEqual(a.x, b.x)
      && IsNearlyEqual(a.y, b.y) && IsNearlyEqual(a.z, b.z);
  };
  return near(value, identity) || near(value, neg_identity);
}

inline auto IsIdentityTranslation(const Vec3& value) noexcept -> bool
{
  return IsNearlyEqual(value.x, 0.0F) && IsNearlyEqual(value.y, 0.0F)
    && IsNearlyEqual(value.z, 0.0F);
}

inline auto IsIdentityScale(const Vec3& value) noexcept -> bool
{
  return IsNearlyEqual(value.x, 1.0F) && IsNearlyEqual(value.y, 1.0F)
    && IsNearlyEqual(value.z, 1.0F);
}

inline auto IsUniformScale(const Vec3& value) noexcept -> bool
{
  return IsNearlyEqual(value.x, value.y) && IsNearlyEqual(value.y, value.z);
}

inline auto IsFinitePositiveScale(const Vec3& value) noexcept -> bool
{
  return std::isfinite(value.x) && std::isfinite(value.y)
    && std::isfinite(value.z) && value.x > 0.0F && value.y > 0.0F
    && value.z > 0.0F;
}

inline auto AllowsNonUniformScale(const CollisionShape& shape) noexcept -> bool
{
  return std::visit(
    [](const auto& value) -> bool {
      using T = std::decay_t<decltype(value)>;
      return std::is_same_v<T, BoxShape> || std::is_same_v<T, TriangleMeshShape>
        || std::is_same_v<T, HeightFieldShape>
        || std::is_same_v<T, CompoundShape>;
    },
    shape);
}

inline auto IgnoresScale(const CollisionShape& shape) noexcept -> bool
{
  return std::visit(
    [](const auto& value) -> bool {
      using T = std::decay_t<decltype(value)>;
      return std::is_same_v<T, PlaneShape>
        || std::is_same_v<T, WorldBoundaryShape>;
    },
    shape);
}

inline auto ApplyShapeLocalTransform(const CollisionShape& source_shape,
  JPH::RefConst<JPH::Shape> base_shape, const Vec3& local_position,
  const Quat& local_rotation, const Vec3& local_scale)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  if (!IsFiniteTranslation(local_position)
    || !IsValidRotation(local_rotation)) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const auto normalized_local_rotation = NormalizeRotation(local_rotation);

  if (!IgnoresScale(source_shape)) {
    if (!IsFinitePositiveScale(local_scale)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (!IsUniformScale(local_scale) && !AllowsNonUniformScale(source_shape)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (!IsIdentityScale(local_scale)) {
      const auto jolt_scale = ToJoltVec3(local_scale);
      if (!base_shape->IsValidScale(jolt_scale)) {
        return Err(PhysicsError::kInvalidArgument);
      }
      base_shape = JPH::RefConst<JPH::Shape> {
        new JPH::ScaledShape(base_shape.GetPtr(), jolt_scale),
      };
    }
  }

  if (!IsIdentityTranslation(local_position)
    || !IsIdentityRotation(normalized_local_rotation)) {
    base_shape = JPH::RefConst<JPH::Shape> { new JPH::RotatedTranslatedShape(
      ToJoltVec3(local_position), ToJoltQuat(normalized_local_rotation),
      base_shape.GetPtr()) };
  }

  return Ok(std::move(base_shape));
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
      } else if constexpr (std::is_same_v<T, CylinderShape>) {
        if (value.radius <= 0.0F || value.half_height <= 0.0F) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(JPH::RefConst<JPH::Shape> {
          new JPH::CylinderShape(value.half_height, value.radius),
        });
      } else if constexpr (std::is_same_v<T, PlaneShape>) {
        const auto normal_len2 = glm::dot(value.normal, value.normal);
        if (!(normal_len2 > 0.0F)) {
          return Err(PhysicsError::kInvalidArgument);
        }
        const auto normal = glm::normalize(value.normal);
        const auto plane = JPH::Plane(ToJoltVec3(normal), value.distance);
        return Ok(JPH::RefConst<JPH::Shape> { new JPH::PlaneShape(plane) });
      } else if constexpr (std::is_same_v<T, WorldBoundaryShape>) {
        return MakeWorldBoundaryShape(value);
      } else if constexpr (std::is_same_v<T, ConeShape>) {
        if (value.radius <= 0.0F || value.half_height <= 0.0F) {
          return Err(PhysicsError::kInvalidArgument);
        }
        if (value.cooked_payload.payload_type != ShapePayloadType::kConvex) {
          return Err(PhysicsError::kInvalidArgument);
        }
        const auto restored = RestoreCookedJoltShape(value.cooked_payload);
        if (restored.has_error()) {
          return Err(restored.error());
        }
        if (restored.value()->GetType() != JPH::EShapeType::Convex) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(restored.value());
      } else if constexpr (std::is_same_v<T, ConvexHullShape>) {
        if (value.cooked_payload.payload_type != ShapePayloadType::kConvex) {
          return Err(PhysicsError::kInvalidArgument);
        }
        const auto restored = RestoreCookedJoltShape(value.cooked_payload);
        if (restored.has_error()) {
          return Err(restored.error());
        }
        if (restored.value()->GetType() != JPH::EShapeType::Convex) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(restored.value());
      } else if constexpr (std::is_same_v<T, TriangleMeshShape>) {
        if (value.cooked_payload.payload_type != ShapePayloadType::kMesh) {
          return Err(PhysicsError::kInvalidArgument);
        }
        const auto restored = RestoreCookedJoltShape(value.cooked_payload);
        if (restored.has_error()) {
          return Err(restored.error());
        }
        if (restored.value()->GetType() != JPH::EShapeType::Mesh) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(restored.value());
      } else if constexpr (std::is_same_v<T, HeightFieldShape>) {
        if (value.cooked_payload.payload_type
          != ShapePayloadType::kHeightField) {
          return Err(PhysicsError::kInvalidArgument);
        }
        const auto restored = RestoreCookedJoltShape(value.cooked_payload);
        if (restored.has_error()) {
          return Err(restored.error());
        }
        if (restored.value()->GetType() != JPH::EShapeType::HeightField) {
          return Err(PhysicsError::kInvalidArgument);
        }
        return Ok(restored.value());
      } else if constexpr (std::is_same_v<T, CompoundShape>) {
        if (value.cooked_payload.payload_type != ShapePayloadType::kCompound) {
          return Err(PhysicsError::kInvalidArgument);
        }
        if (value.cooked_payload.data.empty()) {
          return Err(PhysicsError::kShapeCompoundZeroChildren);
        }
        const auto restored = RestoreCookedJoltShape(value.cooked_payload);
        if (restored.has_error()) {
          return Err(restored.error());
        }
        if (restored.value()->GetType() != JPH::EShapeType::Compound) {
          return Err(PhysicsError::kInvalidArgument);
        }
        const auto& compound
          = static_cast<const JPH::CompoundShape&>(*restored.value());
        if (compound.GetNumSubShapes() == 0) {
          return Err(PhysicsError::kShapeCompoundZeroChildren);
        }
        if (!ValidateCompoundChildScaleContract(compound)) {
          return Err(PhysicsError::kShapeCompoundChildScaleContractViolation);
        }
        return Ok(restored.value());
      } else {
        return Err(PhysicsError::kNotImplemented);
      }
    },
    shape);
}

} // namespace oxygen::physics::jolt
