# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

set(description "A CLI tool for building Oxygen pak archives.")
set(pak_tool_version "0.1")

asap_module_declare(
  MODULE_NAME "Oxygen.Cooker.PakTool"
  DESCRIPTION description
  WITHOUT_VERSION_H
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json REQUIRED)

include(JsonSchemaHelpers)

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "App.cpp"
  "App.h"
  "ArtifactPublication.cpp"
  "ArtifactPublication.h"
  "BuildReportJson.cpp"
  "BuildReportJson.h"
  "CliBuilder.cpp"
  "CliBuilder.h"
  "CommandExecution.cpp"
  "CommandExecution.h"
  "PakToolOptions.h"
  "RequestPreparation.cpp"
  "RequestPreparation.h"
  "RequestSnapshot.h"
  "ScriptSealing.cpp"
  "ScriptSealing.h"
  "main.cpp"
)

oxygen_embed_json_schemas(
  TARGET ${META_MODULE_TARGET}
  OUTPUT_HEADER
    "${CMAKE_BINARY_DIR}/generated/Oxygen/Cooker/Tools/PakTool/Internal/PakBuildReport_schema.h"
  NAMESPACE "oxygen::content::pak::tool"
  INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/generated"
  CHUNK_SIZE 8192
  SCHEMAS
    kPakBuildReportSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Schemas/oxygen.pak-build-report.schema.json"
)

# Convenience build target: allow building by module name (matches OUTPUT_NAME)
# in addition to the generated target name.
add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
arrange_target_files_for_ide(${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)
target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::base
    oxygen::cooker
    oxygen::clap
    nlohmann_json::nlohmann_json
)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    OXYGEN_PAKTOOL_VERSION="${pak_tool_version}"
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
  add_subdirectory("Test")
endif()

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

if(${META_PROJECT_ID}_INSTALL)
  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/Schemas/oxygen.pak-build-report.schema.json"
    DESTINATION ${OXYGEN_INSTALL_DATA}/schemas
    COMPONENT ${META_PROJECT_NAME}_data
  )
endif()

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
