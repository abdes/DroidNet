# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Shader baking (Task 14): build-time OXSL v1 library
# ------------------------------------------------------------------------------

# Find all HLSL entrypoints under the shader layout.
# Note: some entrypoints (e.g. IBL filtering) live outside Passes/.
file(GLOB_RECURSE HLSL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl")

# Shared include files (HLSL headers)
file(GLOB_RECURSE HLSLI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsli")

set(SHADER_LIBRARY_OUTPUT "${CMAKE_SOURCE_DIR}/bin/Oxygen/shaders.bin")

# The ShaderBake tool target name (executable)
set(OXYGEN_D3D12_SHADERBAKE_TARGET "oxygen-graphics-direct3d12-shaderbake")

# Resolve deployed runtime DLL folder per active build configuration.
# ASan builds use out/install/Asan/bin while non-ASan builds follow $<CONFIG>.
set(_OXYGEN_IS_ASAN_BUILD OFF)
if(CMAKE_BINARY_DIR MATCHES "asan")
  set(_OXYGEN_IS_ASAN_BUILD ON)
endif()

set(_OXYGEN_DEPLOY_CONFIG "$<IF:$<BOOL:${_OXYGEN_IS_ASAN_BUILD}>,Asan,$<CONFIG>>")
set(_OXYGEN_DEPLOY_BIN_DIR "$<SHELL_PATH:${CMAKE_SOURCE_DIR}/out/install/${_OXYGEN_DEPLOY_CONFIG}/bin>")
if(WIN32)
  # Visual Studio custom build commands are sensitive to ';' list splitting.
  # Use CMake's env modifier to prepend the runtime DLL folder safely.
  set(_OXYGEN_SHADERBAKE_ENV_ARGS
    --modify
    "PATH=path_list_prepend:${_OXYGEN_DEPLOY_BIN_DIR}"
  )
else()
  set(_OXYGEN_SHADERBAKE_ENV_ARGS
    "PATH=${_OXYGEN_DEPLOY_BIN_DIR}:$ENV{PATH}"
  )
endif()

# Convert to native paths for the custom command to ensure robustness in Visual Studio/cmd.exe
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/bin/Oxygen" _OXYGEN_BIN_DIR)
file(TO_NATIVE_PATH "${SHADER_LIBRARY_OUTPUT}" _SHADER_LIBRARY_OUTPUT_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" _WORKSPACE_ROOT_NATIVE)

add_custom_command(
  OUTPUT
    "${SHADER_LIBRARY_OUTPUT}"
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory "${_OXYGEN_BIN_DIR}"
  COMMAND
    "${CMAKE_COMMAND}" -E env ${_OXYGEN_SHADERBAKE_ENV_ARGS}
    "$<TARGET_FILE:${OXYGEN_D3D12_SHADERBAKE_TARGET}>" --workspace-root
    "${_WORKSPACE_ROOT_NATIVE}" --out "${_SHADER_LIBRARY_OUTPUT_NATIVE}"
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
