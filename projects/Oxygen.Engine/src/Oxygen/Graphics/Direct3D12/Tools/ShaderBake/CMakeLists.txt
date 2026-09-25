# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

set(description "A tool to compile and package Direct3D12 shaders.")
asap_module_declare(
  MODULE_NAME "Oxygen.Graphics.Direct3D12.ShaderBake"
  DESCRIPTION "${description}"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Input System Example ------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "ActionKey.cpp"
  "ActionKey.h"
  "Bake.cpp"
  "Bake.h"
  "BuildPaths.cpp"
  "BuildPaths.h"
  "BuildState.cpp"
  "BuildState.h"
  "Catalog.cpp"
  "Catalog.h"
  "CompileProfile.h"
  "DirtyAnalysis.cpp"
  "DirtyAnalysis.h"
  "DxcShaderCompiler.cpp"
  "DxcShaderCompiler.h"
  "Execution.cpp"
  "Execution.h"
  "FileFingerprint.cpp"
  "FileFingerprint.h"
  "FinalArchivePack.cpp"
  "FinalArchivePack.h"
  "Inspect.cpp"
  "Inspect.h"
  "Manifest.cpp"
  "Manifest.h"
  "ModuleArtifact.cpp"
  "ModuleArtifact.h"
  "Reflect.cpp"
  "Reflect.h"
  "RequestCompilation.cpp"
  "RequestCompilation.h"
  "TrackingIncludeHandler.cpp"
  "TrackingIncludeHandler.h"
  "main.cpp"
  "../../Shaders/EngineShaderCatalog.h"
  "../../Shaders/ShaderCatalogBuilder.h"
)

# Convenience build target: allow building by module name (matches OUTPUT_NAME)
# in addition to the canonical oxygen-graphics-direct3d12-shaderbake target.
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
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::graphics-common
    oxygen::clap
    oxygen::serio
    nlohmann_json::nlohmann_json
    dxc::dxcompiler
)

# dxil.dll is loaded by DXC at runtime; it is not an import-library dependency.
oxygen_stage_dxc_runtime(${META_MODULE_TARGET})

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
