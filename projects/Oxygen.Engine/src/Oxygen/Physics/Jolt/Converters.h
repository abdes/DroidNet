//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Jolt/Jolt.h> // IWYU pragma: keep (must always be first)

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

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
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <Jolt/Physics/EActivation.h>

#include <Oxygen/Core/Transforms/Decompose.h>
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
  // Default mapping for non-scene-authored callers.
  // Scene-authored layer/mask routing is resolved by JoltWorld.
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
  const auto blob = std::string(payload.data.begin(), payload.data.end());
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
  return (value.w * value.w) + (value.x * value.x) + (value.y * value.y)
    + (value.z * value.z);
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
  const auto near = [](const Quat& a, const Quat& b) -> bool {
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

//! The engine's uniform scale rule, which the cooker applies too.
inline auto IsUniformScale(const Vec3& value) noexcept -> bool
{
  return transforms::IsUniformScale(value);
}

inline auto IsFinitePositiveScale(const Vec3& value) noexcept -> bool
{
  return std::isfinite(value.x) && std::isfinite(value.y)
    && std::isfinite(value.z) && value.x > 0.0F && value.y > 0.0F
    && value.z > 0.0F;
}

//! Whether `T` is one of `Ts`.
template <typename T, typename... Ts>
inline constexpr bool kIsAnyOf = (std::is_same_v<T, Ts> || ...);

//! Whether `value` holds one of the types `Wanted`; a type the variant does
//! not have is never held.
template <typename... Wanted, typename... Alternatives>
constexpr auto HoldsAnyOf(const std::variant<Alternatives...>& value) noexcept
  -> bool
{
  return (...
    || (kIsAnyOf<Alternatives, Wanted...>
      && std::holds_alternative<Alternatives>(value)));
}

//! Whether the shape `shape` holds takes a per-axis scale.
template <typename ShapeVariant>
inline auto AllowsNonUniformScale(const ShapeVariant& shape) noexcept -> bool
{
  return HoldsAnyOf<BoxShape, TriangleMeshShape, HeightFieldShape,
    CompoundShape>(shape);
}

//! Whether the shape `shape` holds is built without its scale.
template <typename ShapeVariant>
inline auto IgnoresScale(const ShapeVariant& shape) noexcept -> bool
{
  return HoldsAnyOf<PlaneShape, WorldBoundaryShape>(shape);
}

//! Places `base_shape`, built from `source_shape`, at its local transform.
template <typename ShapeVariant>
inline auto ApplyShapeLocalTransform(const ShapeVariant& source_shape,
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

//! Adapts Jolt's Y-axis primitive basis to Oxygen before authored transforms.
inline auto OrientAxialShapeToOxygen(const JPH::RefConst<JPH::Shape>& shape)
  -> JPH::RefConst<JPH::Shape>
{
  const auto rotation = JPH::Quat::sFromTo(
    JPH::Vec3::sAxisY(), ToJoltVec3(oxygen::space::move::Up));
  return JPH::RefConst<JPH::Shape> { new JPH::RotatedTranslatedShape(
    JPH::Vec3::sZero(), rotation, shape.GetPtr()) };
}

//! Restores a cooked shape and checks it has the Jolt type `expected`.
inline auto RestoreCookedShapeOfType(const CookedShapePayload& payload,
  const ShapePayloadType payload_type, const JPH::EShapeType expected)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  if (payload.payload_type != payload_type) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const auto restored = RestoreCookedJoltShape(payload);
  if (restored.has_error()) {
    return Err(restored.error());
  }
  if (restored.value()->GetType() != expected) {
    return Err(PhysicsError::kInvalidArgument);
  }
  return Ok(restored.value());
}

//! Builds the Jolt shape of one shape alternative, in the shape's space.
struct ShapeBuilder final {
  template <typename T>
  auto operator()(const T& value) const
    -> PhysicsResult<JPH::RefConst<JPH::Shape>>
  {
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
      // Jolt's convex radius must fit inside the box.
      const auto convex_radius = std::min({
        JPH::cDefaultConvexRadius,
        value.extents.x,
        value.extents.y,
        value.extents.z,
      });
      return Ok(JPH::RefConst<JPH::Shape> {
        new JPH::BoxShape(ToJoltVec3(value.extents), convex_radius),
      });
    } else if constexpr (std::is_same_v<T, CapsuleShape>) {
      if (!std::isfinite(value.radius) || !std::isfinite(value.half_height)
        || value.radius <= 0.0F || value.half_height < 0.0F) {
        return Err(PhysicsError::kInvalidArgument);
      }
      // A zero-length cylinder is the exact sphere limit. Jolt's capsule
      // constructor requires positive length; its settings API uses the
      // same sphere representation for this limit.
      if (value.half_height == 0.0F) {
        return Ok(
          JPH::RefConst<JPH::Shape> { new JPH::SphereShape(value.radius) });
      }
      return Ok(OrientAxialShapeToOxygen(JPH::RefConst<JPH::Shape> {
        new JPH::CapsuleShape(value.half_height, value.radius),
      }));
    } else if constexpr (std::is_same_v<T, CylinderShape>) {
      if (!std::isfinite(value.radius) || !std::isfinite(value.half_height)
        || value.radius <= 0.0F || value.half_height <= 0.0F) {
        return Err(PhysicsError::kInvalidArgument);
      }
      // Jolt's convex radius must fit inside the cylinder.
      const auto convex_radius = std::min(
        { JPH::cDefaultConvexRadius, value.half_height, value.radius });
      return Ok(OrientAxialShapeToOxygen(JPH::RefConst<JPH::Shape> {
        new JPH::CylinderShape(value.half_height, value.radius, convex_radius),
      }));
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
      if (!std::isfinite(value.radius) || !std::isfinite(value.half_height)
        || value.radius <= 0.0F || value.half_height <= 0.0F) {
        return Err(PhysicsError::kInvalidArgument);
      }
      // A cylinder whose top radius is zero: the apex at +half_height. Its
      // sharp apex leaves no room for a convex radius.
      const auto settings = JPH::TaperedCylinderShapeSettings(
        value.half_height, 0.0F, value.radius, 0.0F);
      const auto created = settings.Create();
      if (created.HasError()) {
        return Err(PhysicsError::kInvalidArgument);
      }
      return Ok(OrientAxialShapeToOxygen(created.Get()));
    } else if constexpr (std::is_same_v<T, ConvexHullShape>) {
      return RestoreCookedShapeOfType(value.cooked_payload,
        ShapePayloadType::kConvex, JPH::EShapeType::Convex);
    } else if constexpr (std::is_same_v<T, TriangleMeshShape>) {
      return RestoreCookedShapeOfType(
        value.cooked_payload, ShapePayloadType::kMesh, JPH::EShapeType::Mesh);
    } else if constexpr (std::is_same_v<T, HeightFieldShape>) {
      // The cooked grid's heights run along Jolt +Y, like an axial shape.
      auto restored = RestoreCookedShapeOfType(value.cooked_payload,
        ShapePayloadType::kHeightField, JPH::EShapeType::HeightField);
      if (restored.has_error()) {
        return restored;
      }
      return Ok(OrientAxialShapeToOxygen(restored.value()));
    } else if constexpr (std::is_same_v<T, CompoundShape>) {
      return MakeCompound(value);
    } else {
      static_assert(sizeof(T) == 0, "unhandled collision shape alternative");
    }
  }

  //! Assembles the children, each at its local transform. A compound of one
  //! child is that child at its transform, the form Jolt asks for.
  [[nodiscard]] auto MakeCompound(const CompoundShape& compound) const
    -> PhysicsResult<JPH::RefConst<JPH::Shape>>
  {
    if (compound.children.empty()) {
      return Err(PhysicsError::kShapeCompoundZeroChildren);
    }
    auto settings = JPH::StaticCompoundShapeSettings {};
    auto only_child = JPH::RefConst<JPH::Shape> {};
    for (const auto& child : compound.children) {
      auto built = std::visit(*this, child.shape);
      if (built.has_error()) {
        return built;
      }
      auto placed = ApplyShapeLocalTransform(child.shape, built.value(),
        child.local_position, child.local_rotation, child.local_scale);
      if (placed.has_error()) {
        return placed;
      }
      if (!ValidateCompoundChildScaleContract(*placed.value())) {
        return Err(PhysicsError::kShapeCompoundChildScaleContractViolation);
      }
      only_child = placed.value();
      settings.AddShape(
        JPH::Vec3::sZero(), JPH::Quat::sIdentity(), placed.value().GetPtr());
    }
    if (compound.children.size() == 1U) {
      return Ok(only_child);
    }
    const auto created = settings.Create();
    if (created.HasError()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    return Ok(created.Get());
  }
};

inline auto MakeShape(const CollisionShape& shape)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  return std::visit(ShapeBuilder {}, shape);
}

//! Whether only a static body takes `shape`: a triangle mesh, height field,
//! plane or world boundary, or a compound holding one.
inline auto MustBeStatic(const CollisionShape& shape) noexcept -> bool
{
  const auto static_only = [](const auto& value) -> bool {
    return HoldsAnyOf<TriangleMeshShape, HeightFieldShape, PlaneShape,
      WorldBoundaryShape>(value);
  };
  if (const auto* compound = std::get_if<CompoundShape>(&shape)) {
    return std::ranges::any_of(
      compound->children, [&](const CompoundShapeChild& child) -> bool {
        return static_only(child.shape);
      });
  }
  return static_only(shape);
}

} // namespace oxygen::physics::jolt
