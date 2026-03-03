//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared Physics PAK format enum catalogs.
//
// All enums here are sourced as the canonical truth for both the serialized
// PakFormat v7 structures and the runtime Physics module headers. Any update
// to values here automatically propagates to both sides.
//
// Each section below declares a specific catalog macro. All macros are
// undefined at the end of the file.
//
// ============================================================================
// PhysicsResourceFormat
// ============================================================================
// Serialized format of the cooked physics binary blob stored in a
// PhysicsResourceDesc entry.
//
// Expected consumer macro:
//   OXPHYS_RESOURCE_FORMAT(name, value)
//
#ifndef OXPHYS_RESOURCE_FORMAT
#  define OXPHYS_RESOURCE_FORMAT(name, value)
#endif

OXPHYS_RESOURCE_FORMAT(kJoltShapeBinary, 0)
OXPHYS_RESOURCE_FORMAT(kJoltConstraintBinary, 1)
OXPHYS_RESOURCE_FORMAT(kJoltSoftBodySharedSettingsBinary, 2)
OXPHYS_RESOURCE_FORMAT(kJoltVehicleConstraintBinary, 3)
OXPHYS_RESOURCE_FORMAT(kPhysXSoftBodySettingsBinary, 4)

#undef OXPHYS_RESOURCE_FORMAT

// ============================================================================
// PhysicsCombineMode
// ============================================================================
// How friction or restitution values are combined at a contact point.
// Maps directly to JPH::EConstraintSpace / contact combine mode.
//
// Expected consumer macro:
//   OXPHYS_COMBINE_MODE(name, value)
//
#ifndef OXPHYS_COMBINE_MODE
#  define OXPHYS_COMBINE_MODE(name, value)
#endif

OXPHYS_COMBINE_MODE(kAverage, 0)
OXPHYS_COMBINE_MODE(kMin, 1)
OXPHYS_COMBINE_MODE(kMax, 2)
OXPHYS_COMBINE_MODE(kMultiply, 3)

#undef OXPHYS_COMBINE_MODE

// ============================================================================
// CollisionShapeCategory
// ============================================================================
// Broad category of collision shape topology. Used by CollisionShapeAssetDesc
// to select appropriate Jolt deserialization path.
//
// Expected consumer macro:
//   OXPHYS_SHAPE_CATEGORY(name, value)
//
#ifndef OXPHYS_SHAPE_CATEGORY
#  define OXPHYS_SHAPE_CATEGORY(name, value)
#endif

OXPHYS_SHAPE_CATEGORY(kConvex, 0)
OXPHYS_SHAPE_CATEGORY(kMesh, 1)
OXPHYS_SHAPE_CATEGORY(kCompound, 2)

#undef OXPHYS_SHAPE_CATEGORY

// ============================================================================
// PhysicsBodyType
// ============================================================================
// The motion type of a physics body. Maps to JPH::EMotionType.
//
// Expected consumer macro:
//   OXPHYS_BODY_TYPE(name, value)
//
#ifndef OXPHYS_BODY_TYPE
#  define OXPHYS_BODY_TYPE(name, value)
#endif

OXPHYS_BODY_TYPE(kStatic, 0)
OXPHYS_BODY_TYPE(kDynamic, 1)
OXPHYS_BODY_TYPE(kKinematic, 2)

#undef OXPHYS_BODY_TYPE

// ============================================================================
// PhysicsMotionQuality
// ============================================================================
// The collision detection quality for a moving body.
// Maps to JPH::EMotionQuality.
//
// Expected consumer macro:
//   OXPHYS_MOTION_QUALITY(name, value)
//
#ifndef OXPHYS_MOTION_QUALITY
#  define OXPHYS_MOTION_QUALITY(name, value)
#endif

OXPHYS_MOTION_QUALITY(kDiscrete, 0)
OXPHYS_MOTION_QUALITY(kLinearCast, 1)

#undef OXPHYS_MOTION_QUALITY

// ============================================================================
// SoftBodyTetherMode
// ============================================================================
// Tether constraint topology for soft bodies.
//
// Expected consumer macro:
//   OXPHYS_SOFT_BODY_TETHER_MODE(name, value)
//
#ifndef OXPHYS_SOFT_BODY_TETHER_MODE
#  define OXPHYS_SOFT_BODY_TETHER_MODE(name, value)
#endif

OXPHYS_SOFT_BODY_TETHER_MODE(kNone, 0)
OXPHYS_SOFT_BODY_TETHER_MODE(kEuclidean, 1)
OXPHYS_SOFT_BODY_TETHER_MODE(kGeodesic, 2)

#undef OXPHYS_SOFT_BODY_TETHER_MODE

// ============================================================================
// VehicleWheelSide
// ============================================================================
// Author-authored wheel side for deterministic axle/steering mapping.
//
// Expected consumer macro:
//   OXPHYS_VEHICLE_WHEEL_SIDE(name, value)
//
#ifndef OXPHYS_VEHICLE_WHEEL_SIDE
#  define OXPHYS_VEHICLE_WHEEL_SIDE(name, value)
#endif

OXPHYS_VEHICLE_WHEEL_SIDE(kLeft, 0)
OXPHYS_VEHICLE_WHEEL_SIDE(kRight, 1)

#undef OXPHYS_VEHICLE_WHEEL_SIDE

// ============================================================================
// AggregateAuthority
// ============================================================================
// Determines which system owns the transforms for bodies inside an aggregate.
//
// Expected consumer macro:
//   OXPHYS_AGGREGATE_AUTHORITY(name, value)
//
#ifndef OXPHYS_AGGREGATE_AUTHORITY
#  define OXPHYS_AGGREGATE_AUTHORITY(name, value)
#endif

OXPHYS_AGGREGATE_AUTHORITY(kSimulation, 0)
OXPHYS_AGGREGATE_AUTHORITY(kCommand, 1)

#undef OXPHYS_AGGREGATE_AUTHORITY

// ============================================================================
// PhysicsBindingType
// ============================================================================
// Identifies which type of physics binding record is stored in a
// PhysicsComponentTableDesc table within a PhysicsSceneAssetDesc.
//
// This enum is ENTIRELY INDEPENDENT of the Scene ComponentType system.
// Physics binding records are NOT scene node components; they are sidecar
// domain bindings that reference scene nodes by index but live outside the
// Scene's composition architecture.
//
// Expected consumer macro:
//   OXPHYS_BINDING_TYPE(name, value)
//
#ifndef OXPHYS_BINDING_TYPE
#  define OXPHYS_BINDING_TYPE(name, value)
#endif

OXPHYS_BINDING_TYPE(kUnknown, 0)
OXPHYS_BINDING_TYPE(kRigidBody, 0x59485052)   // 'RPHY'
OXPHYS_BINDING_TYPE(kCollider, 0x4C4F4350)    // 'PCOL'
OXPHYS_BINDING_TYPE(kCharacter, 0x52484350)   // 'PCHR'
OXPHYS_BINDING_TYPE(kSoftBody, 0x42534650)    // 'PFSB'
OXPHYS_BINDING_TYPE(kJoint, 0x544E4A50)       // 'PJNT'
OXPHYS_BINDING_TYPE(kVehicle, 0x4C485650)     // 'PVHL'
OXPHYS_BINDING_TYPE(kVehicleWheel, 0x48575650) // 'PVWH'
OXPHYS_BINDING_TYPE(kAggregate, 0x47474150)   // 'PAGG'

#undef OXPHYS_BINDING_TYPE
