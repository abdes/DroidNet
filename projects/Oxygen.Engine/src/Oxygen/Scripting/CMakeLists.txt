# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Scripting"
  DESCRIPTION "Oxygen scripting module"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(Luau CONFIG REQUIRED)

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
      "OXGN_SCRP_EXPORTS"
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
  OXYGEN_SCRIPTING_HEADERS
  "api_export.h"
  "Bindings/BindingRegistry.h"
  "Bindings/LuaBindingCommon.h"
  "Compilers/LuauScriptCompiler.h"
  "Execution/CompiledScriptExecutable.h"
  "Execution/FailedScriptExecutable.h"
  "IScriptSourceResolver.h"
  "Module/ScriptingModule.h"
  "Resolver/EmbeddedSourceResolver.h"
  "Resolver/ExternalFileResolver.h"
  "Resolver/ScriptSourceResolver.h"
)
set(
  OXYGEN_SCRIPTING_PRIVATE_SOURCES
  "Bindings/BindingRegistry.cpp"
  "Bindings/LuaBindingCommon.cpp"
  "Bindings/Packs/Core/AppBindings.cpp"
  "Bindings/Packs/Core/CoreBindingPack.cpp"
  "Bindings/Packs/Core/TimeBindings.cpp"
  "Bindings/Packs/Scene/SceneBindingPack.cpp"
  "Bindings/Packs/Scene/SceneBindings.cpp"
  "Bindings/Packs/Scene/TransformBindings.cpp"
  "Compilers/LuauScriptCompiler.cpp"
  "Module/ScriptingModule.cpp"
  "Resolver/EmbeddedSourceResolver.cpp"
  "Resolver/ExternalFileResolver.cpp"
  "Resolver/ScriptSourceResolver.cpp"
)

add_subdirectory("Bindings")

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_SCRIPTING_PRIVATE_SOURCES}
    ${OXYGEN_SCRIPTING_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_SCRIPTING_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET} EXCLUDE_PATTERNS "bindings/")

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_20)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_SCRP_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    Luau::Compiler
    Luau::VM
    oxygen::base
    oxygen::config
    oxygen::core
    oxygen::data
    oxygen::engine
    oxygen::scene
    oxygen::scripting-bindings-contracts
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
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
