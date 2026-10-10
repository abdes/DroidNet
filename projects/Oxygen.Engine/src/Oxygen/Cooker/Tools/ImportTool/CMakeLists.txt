# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

set(description "A CLI tool for invoking async import jobs.")
set(import_tool_version "0.1")
asap_module_declare(
  MODULE_NAME "Oxygen.Cooker.ImportTool"
  DESCRIPTION "${description}"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(ftxui REQUIRED)
find_package(Luau CONFIG REQUIRED)

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

add_library(
  oxygen-cooker-importtool-core
  STATIC
  "BatchCommand.cpp"
  "BatchCommand.h"
  "BuiltinCatalogCommand.cpp"
  "BuiltinCatalogCommand.h"
  "CliBuilder.cpp"
  "CliBuilder.h"
  "FbxCommand.cpp"
  "FbxCommand.h"
  "GlobalOptions.h"
  "GltfCommand.cpp"
  "GltfCommand.h"
  "ImportCommand.h"
  "InputCommand.cpp"
  "InputCommand.h"
  "ImportRunner.cpp"
  "ImportRunner.h"
  "MessageWriter.h"
  "PhysicsSidecarCommand.cpp"
  "PhysicsSidecarCommand.h"
  "ReclaimCommand.cpp"
  "ReclaimCommand.h"
  "ReportJson.cpp"
  "ReportJson.h"
  "SourceAnalysisCommand.cpp"
  "SourceAnalysisCommand.h"
  "SourceInspectionCommand.cpp"
  "SourceInspectionCommand.h"
  "ScriptCommand.cpp"
  "ScriptCommand.h"
  "ScriptingSidecarCommand.cpp"
  "ScriptingSidecarCommand.h"
  "TextureCommand.cpp"
  "TextureCommand.h"
  "UI/BatchViewModel.h"
  "UI/JobViewModel.h"
  "UI/Screens/BatchImportScreen.h"
  "UI/Screens/BatchImportScreen.cpp"
  "UI/Screens/ImportScreen.h"
  "UI/Screens/ImportScreen.cpp"
)
set_target_properties(
  oxygen-cooker-importtool-core
  PROPERTIES
    FOLDER
      "Tools"
)
target_include_directories(
  oxygen-cooker-importtool-core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  oxygen-cooker-importtool-core
  PUBLIC
    oxygen::base
    oxygen::core
    oxygen::cooker
    oxygen::clap
    fmt::fmt-header-only
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    Luau::Compiler
)
target_compile_definitions(
  oxygen-cooker-importtool-core
  PUBLIC
    OXYGEN_IMPORT_TOOL_VERSION="${import_tool_version}"
)

add_executable(${META_MODULE_TARGET} "main.cpp")
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen-cooker-importtool-core
)

add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
arrange_target_files_for_ide(
  oxygen-cooker-importtool-core
  EXCLUDE_PATTERNS
    "main\\.cpp"
)
arrange_target_files_for_ide(${META_MODULE_TARGET} EXCLUDE_PATTERNS ".*")
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      ${META_MODULE_NAME}
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

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
