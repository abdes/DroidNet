//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared shape catalogs for Pak/runtime/script serialization.
//
// This file exposes four catalogs via independent consumer macros:
//   OXPHYS_SHAPE_TYPE(name, value)
//   OXPHYS_SHAPE_PAYLOAD_TYPE(name, value)
//   OXPHYS_WORLD_BOUNDARY_MODE(name, value)
//   OXPHYS_SHAPE_CONSTANT(name, value)

#include <Oxygen/Core/Meta/Data/ResourceIndex.inc>

static_assert(kNoResourceIndex == ResourceIndexT { 0u },
  "ResourceIndex contract violated: kNoResourceIndex must be 0");

// ============================================================================
// ShapeType
// ============================================================================
#ifndef OXPHYS_SHAPE_TYPE
#  define OXPHYS_SHAPE_TYPE(name, value)
#endif

OXPHYS_SHAPE_TYPE(kInvalid, 0)
OXPHYS_SHAPE_TYPE(kSphere, 1)
OXPHYS_SHAPE_TYPE(kCapsule, 2)
OXPHYS_SHAPE_TYPE(kBox, 3)
OXPHYS_SHAPE_TYPE(kCylinder, 4)
OXPHYS_SHAPE_TYPE(kCone, 5)
OXPHYS_SHAPE_TYPE(kConvexHull, 6)
OXPHYS_SHAPE_TYPE(kTriangleMesh, 7)
OXPHYS_SHAPE_TYPE(kHeightField, 8)
OXPHYS_SHAPE_TYPE(kPlane, 9)
OXPHYS_SHAPE_TYPE(kWorldBoundary, 10)
OXPHYS_SHAPE_TYPE(kCompound, 11)

#undef OXPHYS_SHAPE_TYPE

// ============================================================================
// ShapePayloadType
// ============================================================================
#ifndef OXPHYS_SHAPE_PAYLOAD_TYPE
#  define OXPHYS_SHAPE_PAYLOAD_TYPE(name, value)
#endif

OXPHYS_SHAPE_PAYLOAD_TYPE(kInvalid, 0)
OXPHYS_SHAPE_PAYLOAD_TYPE(kConvex, 1)
OXPHYS_SHAPE_PAYLOAD_TYPE(kMesh, 2)
OXPHYS_SHAPE_PAYLOAD_TYPE(kHeightField, 3)
OXPHYS_SHAPE_PAYLOAD_TYPE(kCompound, 4)

#undef OXPHYS_SHAPE_PAYLOAD_TYPE

// ============================================================================
// WorldBoundaryMode
// ============================================================================
#ifndef OXPHYS_WORLD_BOUNDARY_MODE
#  define OXPHYS_WORLD_BOUNDARY_MODE(name, value)
#endif

OXPHYS_WORLD_BOUNDARY_MODE(kInvalid, 0)
OXPHYS_WORLD_BOUNDARY_MODE(kAabbClamp, 1)
OXPHYS_WORLD_BOUNDARY_MODE(kPlaneSet, 2)

#undef OXPHYS_WORLD_BOUNDARY_MODE

// ============================================================================
// Shape Constants
// ============================================================================
#ifndef OXPHYS_SHAPE_CONSTANT
#  define OXPHYS_SHAPE_CONSTANT(name, value)
#endif

OXPHYS_SHAPE_CONSTANT(kInvalidShapePayloadRefIndex, 0)
OXPHYS_SHAPE_CONSTANT(kInvalidShapePayloadType, 0)
OXPHYS_SHAPE_CONSTANT(kInvalidPhysicsMaterialRef, 0u)
OXPHYS_SHAPE_CONSTANT(kShapeIsSensorFalse, 0u)
OXPHYS_SHAPE_CONSTANT(kShapeIsSensorTrue, 1u)

#undef OXPHYS_SHAPE_CONSTANT
