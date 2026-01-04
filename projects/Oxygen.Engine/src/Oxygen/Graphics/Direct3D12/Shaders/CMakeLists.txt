# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Shader baking (Task 14): build-time OXSL v1 library
# ------------------------------------------------------------------------------

# Find all HLSL entrypoints under the pass-oriented shader layout
file(GLOB_RECURSE HLSL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Passes/*.hlsl")

# Shared include files (HLSL headers)
file(GLOB_RECURSE HLSLI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsli")

set(SHADER_LIBRARY_OUTPUT "${CMAKE_SOURCE_DIR}/bin/Oxygen/shaders.bin")

# The ShaderBake tool target name (executable)
set(OXYGEN_D3D12_SHADERBAKE_TARGET "oxygen-graphics-direct3d12-shaderbake")

add_custom_command(
  OUTPUT
    "${SHADER_LIBRARY_OUTPUT}"
  COMMAND
    ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/Oxygen"
  COMMAND
    $<TARGET_FILE:${OXYGEN_D3D12_SHADERBAKE_TARGET}> --workspace-root
    "${CMAKE_SOURCE_DIR}" --out "${SHADER_LIBRARY_OUTPUT}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  VERBATIM
  COMMENT "Baking shaders.bin (OXSL v1) ($<CONFIG> mode)"
  DEPENDS
    ${HLSL_FILES}
    ${HLSLI_FILES}
    ${OXYGEN_D3D12_SHADERBAKE_TARGET}
)

add_custom_target(
  ${META_MODULE_TARGET}_shaders
  ALL
  DEPENDS
    "${SHADER_LIBRARY_OUTPUT}"
)

# Make the main module target depend on the baked shader library
add_dependencies(${META_MODULE_TARGET} ${META_MODULE_TARGET}_shaders)
