# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Scripting.Analysis"
  DESCRIPTION "Static analysis of Oxygen scripts: declarations and diagnostics"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(Luau CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

# Parses scripts without running them, so tools (cooker, editor interop) can
# read declarations and diagnostics without the scripting runtime.
add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_SCAN_EXPORTS"
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
  OXYGEN_SCRIPTING_ANALYSIS_HEADERS
  "api_export.h"
  "DiagnosticCatalog.h"
  "DiagnosticJson.h"
  "DiagnosticRenderer.h"
  "LuauCompiler.h"
  "ScriptAnalyzer.h"
  "ScriptDiagnostic.h"
)
set(
  OXYGEN_SCRIPTING_ANALYSIS_PRIVATE_SOURCES
  "DeclarationExtractor.cpp"
  "DeclarationExtractor.h"
  "DiagnosticCatalog.cpp"
  "DiagnosticJson.cpp"
  "DiagnosticRenderer.cpp"
  "LuauChecks.cpp"
  "LuauChecks.h"
  "LuauCompiler.cpp"
  "ScriptAnalyzer.cpp"
  "ScriptDiagnostic.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_SCRIPTING_ANALYSIS_PRIVATE_SOURCES}
    ${OXYGEN_SCRIPTING_ANALYSIS_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_SCRIPTING_ANALYSIS_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_20)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_SCAN_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::data
  PRIVATE
    Luau::Analysis
    Luau::Ast
    Luau::Compiler
    nlohmann_json::nlohmann_json
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("test")
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
