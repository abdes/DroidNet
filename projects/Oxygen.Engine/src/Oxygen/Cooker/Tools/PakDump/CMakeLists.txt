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
  "A tool to dump the contents of a PAK file and analyze it in different ways."
)
asap_module_declare(
  MODULE_NAME "Oxygen.Cooker.PakDump"
  DESCRIPTION "${description}"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Input System Example ------------------------------------------------------

add_library(
  oxygen-cooker-pakdump-core
  STATIC
  "PakDumpCommand.cpp"
  "PakDumpCommand.h"
  "AssetDumpers.h"
  "AssetDumper.h"
  "AssetDumpHelpers.h"
  "DefaultAssetDumper.h"
  "GeometryAssetDumper.h"
  "InputActionAssetDumper.h"
  "InputMappingContextAssetDumper.h"
  "MaterialAssetDumper.h"
  "PhysicsMaterialAssetDumper.h"
  "CollisionShapeAssetDumper.h"
  "PhysicsSceneAssetDumper.h"
  "SceneAssetDumper.h"
  "ScriptAssetDumper.h"
  "DumpContext.h"
  "PakFileDumper.cpp"
  "PakFileDumper.h"
  "PrintUtils.h"
  "PrintUtils.cpp"
  "DecodedAsset.h"
)
set_target_properties(
  oxygen-cooker-pakdump-core
  PROPERTIES
    FOLDER
      "Tools"
)
target_include_directories(
  oxygen-cooker-pakdump-core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  oxygen-cooker-pakdump-core
  PUBLIC
    oxygen::base
    oxygen::core
    oxygen::data
    oxygen::content
    oxygen::cooker
    oxygen::clap
    oxygen-cooker-toolkit
)

add_executable(${META_MODULE_TARGET} "main.cpp")
target_link_libraries(${META_MODULE_TARGET} PRIVATE oxygen-cooker-pakdump-core)

# Convenience build target: allow building by module name (matches OUTPUT_NAME)
# in addition to the generated target name (e.g. oxygen-cooker-pakdump).
add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
arrange_target_files_for_ide(
  oxygen-cooker-pakdump-core
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

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
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
