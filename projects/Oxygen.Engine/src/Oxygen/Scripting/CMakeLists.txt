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
  "Bindings/Packs/Content/ContentAsyncBindings.h"
  "Bindings/Packs/Content/ContentBindingPack.h"
  "Bindings/Packs/Content/ContentBindings.h"
  "Bindings/Packs/Content/ContentBindingsCommon.h"
  "Bindings/Packs/Content/ContentLifecycleBindings.h"
  "Bindings/Packs/Content/ContentProceduralBindings.h"
  "Bindings/Packs/Content/ContentQueryBindings.h"
  "Bindings/Packs/Content/ContentUserdataBindings.h"
  "Bindings/Packs/Physics/PhysicsBindingPack.h"
  "Bindings/Packs/Physics/PhysicsBindingsCommon.h"
  "Bindings/Packs/Physics/PhysicsBodyBindings.h"
  "Bindings/Packs/Physics/PhysicsCharacterBindings.h"
  "Bindings/Packs/Physics/PhysicsQueryBindings.h"
  "Bindings/Packs/Physics/PhysicsEventsBindings.h"
  "Bindings/Packs/Physics/PhysicsConstantsBindings.h"
  "Bindings/Packs/Physics/PhysicsAggregateBindings.h"
  "Bindings/Packs/Physics/PhysicsArticulationBindings.h"
  "Bindings/Packs/Physics/PhysicsVehicleBindings.h"
  "Bindings/Packs/Physics/PhysicsSoftBodyBindings.h"
  "Bindings/Packs/Scene/SceneBindingPack.h"
  "Bindings/Packs/Scene/SceneBindings.h"
  "Bindings/Packs/Scene/SceneEnvironmentBindings.h"
  "Bindings/Packs/Scene/SceneNodeComponentBindings.h"
  "Bindings/Packs/Scene/SceneNodeBindings.h"
  "Bindings/Packs/Scene/SceneQueryBindings.h"
  "Compilers/LuauScriptCompiler.h"
  "Execution/CompiledScriptExecutable.h"
  "Execution/FailedScriptExecutable.h"
  "Input/InputScriptEventBridge.h"
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
  "Bindings/Packs/Content/ContentAsyncBindings.cpp"
  "Bindings/Packs/Content/ContentBindingPack.cpp"
  "Bindings/Packs/Content/ContentBindings.cpp"
  "Bindings/Packs/Content/ContentBindingsCommon.cpp"
  "Bindings/Packs/Content/ContentLifecycleBindings.cpp"
  "Bindings/Packs/Content/ContentProceduralBindings.cpp"
  "Bindings/Packs/Content/ContentQueryBindings.cpp"
  "Bindings/Packs/Content/ContentUserdataBindings.cpp"
  "Bindings/Packs/Core/AppBindings.cpp"
  "Bindings/Packs/Core/ConventionsBindings.cpp"
  "Bindings/Packs/Core/CoreBindingPack.cpp"
  "Bindings/Packs/Core/EventsBindings.cpp"
  "Bindings/Packs/Core/IdsBindings.cpp"
  "Bindings/Packs/Core/LogBindings.cpp"
  "Bindings/Packs/Core/MathBindings.cpp"
  "Bindings/Packs/Core/TimeBindings.cpp"
  "Bindings/Packs/Input/InputBindingPack.cpp"
  "Bindings/Packs/Input/InputBindings.cpp"
  "Bindings/Packs/Physics/PhysicsBindingPack.cpp"
  "Bindings/Packs/Physics/PhysicsBindingsCommon.cpp"
  "Bindings/Packs/Physics/PhysicsBodyBindings.cpp"
  "Bindings/Packs/Physics/PhysicsCharacterBindings.cpp"
  "Bindings/Packs/Physics/PhysicsQueryBindings.cpp"
  "Bindings/Packs/Physics/PhysicsEventsBindings.cpp"
  "Bindings/Packs/Physics/PhysicsConstantsBindings.cpp"
  "Bindings/Packs/Physics/PhysicsAggregateBindings.cpp"
  "Bindings/Packs/Physics/PhysicsArticulationBindings.cpp"
  "Bindings/Packs/Physics/PhysicsVehicleBindings.cpp"
  "Bindings/Packs/Physics/PhysicsSoftBodyBindings.cpp"
  "Bindings/Packs/Scene/SceneBindingPack.cpp"
  "Bindings/Packs/Scene/SceneBindings.cpp"
  "Bindings/Packs/Scene/SceneEnvironmentBindings.cpp"
  "Bindings/Packs/Scene/SceneNodeCameraBindings.cpp"
  "Bindings/Packs/Scene/SceneNodeComponentBindings.cpp"
  "Bindings/Packs/Scene/SceneNodeLightBindings.cpp"
  "Bindings/Packs/Scene/SceneNodeBindings.cpp"
  "Bindings/Packs/Scene/SceneNodeRenderableBindings.cpp"
  "Bindings/Packs/Scene/SceneNodeScriptingBindings.cpp"
  "Bindings/Packs/Scene/SceneQueryBindings.cpp"
  "Compilers/LuauScriptCompiler.cpp"
  "Input/InputScriptEventBridge.cpp"
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
  PRIVATE
    oxygen::input
    oxygen::physicsmodule
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
