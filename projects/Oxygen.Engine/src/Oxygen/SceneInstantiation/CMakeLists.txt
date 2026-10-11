# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

asap_module_declare(
  MODULE_NAME "Oxygen.SceneInstantiation"
  DESCRIPTION
    "Turns a cooked scene asset into a running scene: nodes, components, instanced scenes, start state and scripts"
)
asap_push_module("${META_MODULE_NAME}")

find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_SCNI_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

set(
  OXYGEN_SCENE_INSTANTIATION_HEADERS
  "api_export.h"
  "EnvironmentHydration.h"
  "SceneInstantiation.h"
)
set(
  OXYGEN_SCENE_INSTANTIATION_PRIVATE_SOURCES
  "EnvironmentHydration.cpp"
  "SceneInstantiation.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_SCENE_INSTANTIATION_PRIVATE_SOURCES}
    ${OXYGEN_SCENE_INSTANTIATION_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_SCENE_INSTANTIATION_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_SCNI_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::content
    oxygen::core
    oxygen::data
    oxygen::oxco
    oxygen::scene
    oxygen::scripting
  PRIVATE
    oxygen::input
    fmt::fmt-header-only
    glm::glm
)

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
endif()

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
