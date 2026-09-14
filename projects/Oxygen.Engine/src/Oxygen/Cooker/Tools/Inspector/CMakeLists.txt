# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

set(
  description
  "A tool to validate and inspect loose cooked content roots and indexes."
)

asap_module_declare(
  MODULE_NAME "Oxygen.Cooker.Inspector"
  DESCRIPTION description
  WITHOUT_VERSION_H
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)
add_executable(
  ${META_MODULE_TARGET}
  "main.cpp"
  "DependencyReport.cpp"
  "DependencyReport.h"
  "AssetKeyMap.cpp"
  "AssetKeyMap.h"
)

oxygen_embed_json_schemas(
  TARGET ${META_MODULE_TARGET}
  OUTPUT_HEADER
    "${CMAKE_BINARY_DIR}/generated/Oxygen/Cooker/Tools/Inspector/AssetKeyMap_schema.h"
  NAMESPACE "oxygen::content::inspection"
  INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/generated"
  SCHEMAS
    kAssetKeyRequestSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Import/Schemas/oxygen.asset-key-request.schema.json"
)

# Convenience build target: allow building by module name (matches OUTPUT_NAME)
# in addition to the generated target name (e.g. oxygen-cooker-inspector).
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

target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::base
    oxygen::data
    oxygen::cooker
    oxygen::content
    oxygen::clap
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
