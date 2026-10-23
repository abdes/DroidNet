# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Content"
  DESCRIPTION "Oxygen asset management."
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(glm CONFIG REQUIRED)
find_package(utf8proc CONFIG REQUIRED)

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
      "OXGN_CNTT_EXPORTS"
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
  OXYGEN_CONTENT_HEADERS
  "api_export.h"
  "PakFile.h"
  "AssetLoader.h"
  "AssetValidation.h"
  "ContentId.h"
  "ContentMounts.h"
  "ContentLoadScope.h"
  "EvictionEvents.h"
  "VirtualPath.h"
  "VirtualPathResolver.h"
  "LoaderContext.h"
  "LoaderFunctions.h"
  "OperationCancelledException.h"
  "ResourceKey.h"
  "ResidencyPolicy.h"
  "ResidencyPin.h"
  "ResourceTable.h"
  "ResourceTypeList.h"
  "InputContextHydration.h"
  "IAssetLoader.h"
  "Layout.h"
  "LooseCookedIndex.h"
)
set(
  OXYGEN_CONTENT_PRIVATE_SOURCES
  "AssetLoader.cpp"
  "AssetLoaderLoading.cpp"
  "AssetValidation.cpp"
  "VirtualPath.cpp"
  "Internal/Generated.UnicodeTables.h"
  "Internal/ContentIdentity.h"
  "Internal/ContentIdentity.cpp"
  "Internal/ContentIdentityRegistry.h"
  "Internal/ContentIdentityRegistry.cpp"
  "Internal/ContentReleaseQueue.h"
  "Internal/ContentReleaseQueue.cpp"
  "Internal/ContentPublication.h"
  "Internal/ContentBindingBundle.h"
  "Internal/ContentBindingBundle.cpp"
  "Internal/ContentSourceRegistry.h"
  "Internal/ContentSourceView.h"
  "Internal/ContentLoadScopeState.h"
  "Internal/ContentSourceRegistry.cpp"
  "Internal/EvictionRegistry.h"
  "Internal/EvictionRegistry.cpp"
  "Internal/InFlightOperationTable.h"
  "Internal/InFlightOperationTable.cpp"
  "Internal/ResourceLoadPipeline.h"
  "Internal/ResourceLoadPipeline.cpp"
  "Internal/SceneCatalogQueryService.h"
  "Internal/SceneCatalogQueryService.cpp"
  "Internal/ScriptQueryService.h"
  "Internal/ScriptQueryService.cpp"
  "Internal/PhysicsBindings.h"
  "Internal/DependencyCollector.h"
  "Internal/ContentFileReader.h"
  "Internal/IContentSource.h"
  "Internal/IContentSource.cpp"
  "Internal/InputContextHydration.cpp"
  "Internal/LooseCookedIndexImpl.h"
  "Internal/LooseCookedIndexImpl.cpp"
  "Internal/LooseCookedIndexValidation.cpp"
  "Internal/LooseCookedIndexCodec.h"
  "Internal/LooseCookedSource.h"
  "Internal/LooseCookedSource.cpp"
  "Internal/PatchResolutionPolicy.h"
  "Internal/PatchResolutionPolicy.cpp"
  "Internal/PakFileSource.h"
  "Internal/ResourceRef.h"
  "Loaders/BufferLoader.h"
  "Loaders/GeometryLoader.h"
  "Loaders/Helpers.h"
  "Loaders/InputActionLoader.h"
  "Loaders/InputMappingContextLoader.h"
  "Loaders/MaterialLoader.h"
  "Loaders/PhysicsResourceLoader.h"
  "Loaders/PhysicsAssetLoader.h"
  "Loaders/PhysicsSceneLoader.h"
  "Loaders/SceneLoader.h"
  "Loaders/ScriptLoader.h"
  "Loaders/TextureLoader.h"
  "LooseCookedIndex.cpp"
  "PakFile.cpp"
  "ResourceKey.cpp"
  "VirtualPathResolver.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_CONTENT_PRIVATE_SOURCES}
    ${OXYGEN_CONTENT_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_CONTENT_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_CNTT_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::composition
    oxygen::console
    oxygen::data
  PRIVATE
    glm::glm
    oxygen::input
    utf8proc::utf8proc
)

# ------------------------------------------------------------------------------
# Unicode tables of the virtual path rules
# ------------------------------------------------------------------------------

# UnicodeCodeGen writes Internal/Generated.UnicodeTables.h into the source tree,
# where it is versioned, from the pinned Unicode Character Database in Unicode/,
# the data the virtual path rules are stated against. A stamp in
# the build tree is the only declared output, so Clean never deletes the
# versioned header. The generator writes the header only when its text changes,
# so a regeneration that changes nothing recompiles nothing.
set(_unicode_tool_dir "${CMAKE_CURRENT_SOURCE_DIR}/Tools/UnicodeCodeGen")
set(_unicode_ucd_dir "${CMAKE_CURRENT_SOURCE_DIR}/Unicode/15.1.0")
set(
  _unicode_tables
  "${CMAKE_CURRENT_SOURCE_DIR}/Internal/Generated.UnicodeTables.h"
)
file(
  GLOB _unicode_tool_scripts
  CONFIGURE_DEPENDS
  "${_unicode_tool_dir}/src/unicode_codegen/*.py"
)
# MSBuild can already have loaded the old dependency list when ZERO_CHECK runs.
# Track the scripts through CMake and expose one stable input to the build rule.
set_property(
  DIRECTORY
  APPEND
  PROPERTY
    CMAKE_CONFIGURE_DEPENDS
      ${_unicode_tool_scripts}
)
set(_unicode_tool_versions "")
foreach(_script IN LISTS _unicode_tool_scripts)
  file(SHA256 "${_script}" _script_hash)
  string(APPEND _unicode_tool_versions "${_script} ${_script_hash}\n")
endforeach()
set(
  _unicode_tool_inventory
  "${CMAKE_CURRENT_BINARY_DIR}/unicode_generator_sources.txt"
)
file(
  GENERATE
  OUTPUT
  "${_unicode_tool_inventory}"
  CONTENT "${_unicode_tool_versions}"
)
set(_unicode_stamp "${CMAKE_CURRENT_BINARY_DIR}/unicode_tables.stamp")
add_custom_command(
  OUTPUT
    ${_unicode_stamp}
  COMMAND
    ${Python3_EXECUTABLE} -m unicode_codegen.cli --ucd "${_unicode_ucd_dir}"
    --cpp-out "${_unicode_tables}"
  COMMAND
    ${CMAKE_COMMAND} -E touch ${_unicode_stamp}
  DEPENDS
    "${_unicode_ucd_dir}/CaseFolding.txt"
    "${_unicode_ucd_dir}/DerivedAge.txt"
    "${_unicode_tool_inventory}"
    "${_unicode_tool_dir}/pyproject.toml"
    "${OXYGEN_PYTHON_LOCKFILE}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "[UnicodeCodeGen] Generating the Unicode tables (source tree)"
  VERBATIM
)
add_custom_target(oxygen-content_unicode_gen DEPENDS ${_unicode_stamp})
add_dependencies(${META_MODULE_TARGET} oxygen-content_unicode_gen)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
  add_subdirectory("test")
endif()
if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Tools/UnicodeCodeGen")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

if(OXYGEN_INSTALL)
  # install-sdk: static
  # The virtual path rules ship with the SDK: consumers built on the installed
  # SDK, such as the editor, generate their Unicode tables from the same pinned
  # database and run the same case table.
  install(
    FILES
      "${_unicode_ucd_dir}/CaseFolding.txt"
      "${_unicode_ucd_dir}/DerivedAge.txt"
      "${_unicode_ucd_dir}/LICENSE.txt"
    DESTINATION "${OXYGEN_INSTALL_DATA}/unicode/15.1.0"
    COMPONENT Oxygen_data
  )
  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/VirtualPathCases.json"
    DESTINATION "${OXYGEN_INSTALL_DATA}/conformance"
    COMPONENT Oxygen_data
  )
endif()

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
