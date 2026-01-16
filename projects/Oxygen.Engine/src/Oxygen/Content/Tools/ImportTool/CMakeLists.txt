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
  "A CLI tool for invoking async import jobs."
)
asap_module_declare(
  MODULE_NAME "Oxygen.Content.ImportTool"
  DESCRIPTION description
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(pdcurses REQUIRED)

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "BatchCommand.cpp"
  "BatchCommand.h"
  "CliBuilder.cpp"
  "CliBuilder.h"
  "ImportCommand.h"
  "ImportManifest.cpp"
  "ImportManifest.h"
  "ImportManifest_schema.h"
  "ImportRunner.cpp"
  "ImportRunner.h"
  "TextureCommand.cpp"
  "TextureCommand.h"
  "TextureImportRequestBuilder.cpp"
  "TextureImportRequestBuilder.h"
  "TextureImportSettings.h"
  "main.cpp"
)

add_custom_target(
  ${META_MODULE_NAME}
  DEPENDS
    ${META_MODULE_TARGET}
)
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
target_compile_definitions(${META_MODULE_TARGET} PRIVATE PDC_WIDE)
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::base
    oxygen::core
    oxygen::content
    oxygen::clap
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
    pdcurses::pdcurses
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
