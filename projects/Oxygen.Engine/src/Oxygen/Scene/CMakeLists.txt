# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Scene"
  DESCRIPTION "Oxygen scene management subsystem"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

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
      "OXGN_SCN_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    "Scene_node_factory.cpp"
    "Scene_reparent.cpp"
    "Scene.cpp"
    "SceneFlags.cpp"
    "SceneNode_Transform.cpp"
    "SceneNode.cpp"
    "SceneNodeImpl.cpp"
    "SceneTraversal.cpp"
    "Detail/GraphData.h"
    "Detail/NodeData.h"
    "Detail/Scene_safecall_impl.cpp"
    "Detail/Scene_safecall_impl.h"
    "Detail/StringConverters.cpp"
    "Detail/TransformComponent.cpp"
    "Detail/TransformComponent.h"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "Scene.h"
      "SceneFlags.h"
      "SceneNode.h"
      "SceneNodeImpl.h"
      "SceneTraversal.h"
      "Types/Flags.h"
      "Types/NodeHandle.h"
      "Types/SceneFlagEnum.h"
)

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_SCN_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    glm::glm
    oxygen::base
    oxygen::core
    oxygen::composition
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
  add_subdirectory("Benchmarks")
endif()

# ------------------------------------------------------------------------------
# API Documentation
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS AND OXYGEN_WITH_DOXYGEN)
  asap_with_doxygen(
    MODULE_NAME ${META_MODULE_TARGET}
    VERSION ${META_VERSION}
    TITLE "\"Scene\""
    BRIEF "\"Scene management for Oxygen\""
    INPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}"
  )
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
