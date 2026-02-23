//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include <Oxygen/Core/Constants.h>

namespace oxygen::physics {

// NOLINTBEGIN(*-macro-usage,*-enum-size)
#define OXPHYS_SHAPE_TYPE(name, value) name = (value),
enum class ShapeType : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
};
#undef OXPHYS_SHAPE_TYPE

#define OXPHYS_SHAPE_PAYLOAD_TYPE(name, value) name = (value),
enum class ShapePayloadType : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
};
#undef OXPHYS_SHAPE_PAYLOAD_TYPE

#define OXPHYS_WORLD_BOUNDARY_MODE(name, value) name = (value),
enum class WorldBoundaryMode : uint32_t {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
};
#undef OXPHYS_WORLD_BOUNDARY_MODE
// NOLINTEND(*-macro-usage,*-enum-size)

struct SphereShape final {
  float radius { 0.5F };
};

struct BoxShape final {
  Vec3 extents { 0.5F, 0.5F, 0.5F };
};

struct CapsuleShape final {
  float radius { 0.5F };
  float half_height { 0.5F };
};

struct CylinderShape final {
  float radius { 0.5F };
  float half_height { 0.5F };
};

struct CookedShapePayload final {
  ShapePayloadType payload_type { ShapePayloadType::kInvalid };
  std::vector<uint8_t> data {};
};

struct ConeShape final {
  float radius { 0.5F };
  float half_height { 0.5F };
  CookedShapePayload cooked_payload {};
};

struct ConvexHullShape final {
  CookedShapePayload cooked_payload {};
};

struct TriangleMeshShape final {
  CookedShapePayload cooked_payload {};
};

struct HeightFieldShape final {
  CookedShapePayload cooked_payload {};
};

struct PlaneShape final {
  Vec3 normal { 0.0F, 0.0F, 1.0F };
  float distance { 0.0F };
};

struct WorldBoundaryShape final {
  WorldBoundaryMode mode { WorldBoundaryMode::kInvalid };
  Vec3 limits_min { 0.0F, 0.0F, 0.0F };
  Vec3 limits_max { 0.0F, 0.0F, 0.0F };
};

struct CompoundShape final {
  CookedShapePayload cooked_payload {};
};

using CollisionShape = std::variant<SphereShape, BoxShape, CapsuleShape,
  CylinderShape, ConeShape, ConvexHullShape, TriangleMeshShape,
  HeightFieldShape, PlaneShape, WorldBoundaryShape, CompoundShape>;

} // namespace oxygen::physics
