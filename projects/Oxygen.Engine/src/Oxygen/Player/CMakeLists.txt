# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Player"
  DESCRIPTION
    "Runs one cooked scene with scripting and input and reports to its launcher"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)

include(JsonSchemaHelpers)

# ==============================================================================
# Build instructions
# ==============================================================================

# Everything but the entry point, so the tests can link it.
add_library(
  oxygen-player-core
  STATIC
  "LaunchFile.cpp"
  "LaunchFile.h"
  "PlayerModule.cpp"
  "PlayerModule.h"
  "PlayerProtocol.cpp"
  "PlayerProtocol.h"
  "SchemaCheck.cpp"
  "SchemaCheck.h"
)
set_target_properties(
  oxygen-player-core
  PROPERTIES
    FOLDER
      "Tools"
)
target_compile_features(oxygen-player-core PUBLIC cxx_std_23)
target_link_libraries(
  oxygen-player-core
  PUBLIC
    oxygen::base
    oxygen::content
    oxygen::core
    oxygen::data
    oxygen::engine
    oxygen::host
    oxygen::sceneinstantiation
    oxygen::scripting
    oxygen::vortex
    nlohmann_json::nlohmann_json
  PRIVATE
    oxygen::input
    nlohmann_json_schema_validator
)

oxygen_embed_json_schemas(
  TARGET oxygen-player-core
  OUTPUT_HEADER
    "${CMAKE_BINARY_DIR}/generated/Oxygen/Player/Internal/PlayerSchemas.h"
  NAMESPACE "oxygen::player"
  INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/generated"
  CHUNK_SIZE 8192
  SCHEMAS
    kPlayerLaunchSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Schemas/oxygen.player-launch.schema.json"
    kPlayerEventsSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Schemas/oxygen.player-events.schema.json"
)

# The executable. The scene observer sync module and the Direct3D12 backend are
# linked so they are built and copied next to it.
add_executable(${META_MODULE_TARGET} "main_impl.cpp")
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::host-main
    oxygen-player-core
    oxygen::clap
    oxygen::config
    oxygen::graphics-common
    oxygen::graphics-direct3d12
    oxygen::scenesync
)
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)
add_custom_command(
  TARGET ${META_MODULE_TARGET}
  POST_BUILD
  COMMAND_EXPAND_LISTS
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:${META_MODULE_TARGET}>
    $<TARGET_FILE_DIR:${META_MODULE_TARGET}>
)

# Convenience build target: build by module name as well as by target name.
add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
arrange_target_files_for_ide(
  oxygen-player-core
  EXCLUDE_PATTERNS
    "main_impl\\.cpp"
)
arrange_target_files_for_ide(${META_MODULE_TARGET} EXCLUDE_PATTERNS ".*")

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("test")
endif()

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

if(${META_PROJECT_ID}_INSTALL)
  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/Schemas/oxygen.player-launch.schema.json"
      "${CMAKE_CURRENT_SOURCE_DIR}/Schemas/oxygen.player-events.schema.json"
    DESTINATION "${OXYGEN_INSTALL_SCHEMAS}"
    COMPONENT ${META_PROJECT_NAME}_data
  )
endif()

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
