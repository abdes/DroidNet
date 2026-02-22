# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Physics"
  DESCRIPTION
    [=[
The Oxygen Physics module provides a high-level abstraction for physics
simulations, supporting multiple backends (initially Jolt and None). It
covers rigid body dynamics, character controllers, collision filtering,
and scene queries like raycasts and sweeps.
]=]
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Backend selection
# ------------------------------------------------------------------------------

set(
  OXYGEN_PHYSICS_BACKEND
  "jolt"
  CACHE STRING
  "Physics backend compiled into Oxygen.Physics (none|jolt)"
)
set_property(
  CACHE
    OXYGEN_PHYSICS_BACKEND
  PROPERTY
    STRINGS
      "none"
      "jolt"
)

set(
  OXYGEN_PHYSICS_SUPPORTED_BACKENDS
  "none"
  "jolt"
)
if(NOT OXYGEN_PHYSICS_BACKEND IN_LIST OXYGEN_PHYSICS_SUPPORTED_BACKENDS)
  message(
    FATAL_ERROR
    "Unsupported OXYGEN_PHYSICS_BACKEND='${OXYGEN_PHYSICS_BACKEND}'. Supported values: ${OXYGEN_PHYSICS_SUPPORTED_BACKENDS}"
  )
endif()
message(STATUS "Oxygen.Physics backend: ${OXYGEN_PHYSICS_BACKEND}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

if(OXYGEN_PHYSICS_BACKEND STREQUAL "jolt")
  find_package(Jolt CONFIG REQUIRED)
endif()

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_PHYS_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

# TODO: do not export internal headers
set(
  OXYGEN_PHYSICS_HEADERS
  "api_export.h"
  "Area/AreaDesc.h"
  "Backend.h"
  "Character/CharacterController.h"
  "CollisionFilter.h"
  "CollisionLayers.h"
  "Events/PhysicsEvents.h"
  "Handles.h"
  "Joint/JointDesc.h"
  "Body/BodyDesc.h"
  "PhysicsError.h"
  "Physics.h"
  "Shape.h"
  "Shape/ShapeDesc.h"
  "Query/Overlap.h"
  "Query/Raycast.h"
  "Query/Sweep.h"
  "System/IAreaApi.h"
  "System/IBodyApi.h"
  "System/ICharacterApi.h"
  "System/IEventApi.h"
  "System/IJointApi.h"
  "System/IPhysicsSystem.h"
  "System/IQueryApi.h"
  "System/IShapeApi.h"
  "System/IWorldApi.h"
  "World/WorldDesc.h"
)

set(
  OXYGEN_PHYSICS_PRIVATE_SOURCES
  "Physics.cpp"
  "Internal/ToStringConverters.cpp"
)

set(OXYGEN_PHYSICS_PRIVATE_HEADERS)

if(OXYGEN_PHYSICS_BACKEND STREQUAL "jolt")
  list(
    APPEND
    OXYGEN_PHYSICS_PRIVATE_SOURCES
    "Jolt/JoltBackend.cpp"
    "Jolt/JoltAreas.cpp"
    "Jolt/JoltBodies.cpp"
    "Jolt/JoltCharacters.cpp"
    "Jolt/JoltEvents.cpp"
    "Jolt/JoltJoints.cpp"
    "Jolt/JoltQueries.cpp"
    "Jolt/JoltShapes.cpp"
    "Jolt/JoltPhysicsSystem.cpp"
    "Jolt/JoltWorld.cpp"
  )
  list(
    APPEND
    OXYGEN_PHYSICS_PRIVATE_HEADERS
    "Jolt/JoltBodies.h"
    "Jolt/JoltBackend.h"
    "Jolt/JoltAreas.h"
    "Jolt/JoltCharacters.h"
    "Jolt/JoltEvents.h"
    "Jolt/JoltJoints.h"
    "Jolt/JoltPhysicsSystem.h"
    "Jolt/JoltQueries.h"
    "Jolt/JoltShapes.h"
    "Jolt/JoltWorld.h"
  )
endif()

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_PHYSICS_PRIVATE_SOURCES}
    ${OXYGEN_PHYSICS_PRIVATE_HEADERS}
    ${OXYGEN_PHYSICS_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_PHYSICS_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_PHYS_STATIC>
  PUBLIC
    $<$<STREQUAL:${OXYGEN_PHYSICS_BACKEND},none>:OXGN_PHYS_BACKEND_NONE>
    $<$<STREQUAL:${OXYGEN_PHYSICS_BACKEND},jolt>:OXGN_PHYS_BACKEND_JOLT>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::core
    $<$<STREQUAL:${OXYGEN_PHYSICS_BACKEND},jolt>:Jolt::Jolt>
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
  add_subdirectory("Test")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
