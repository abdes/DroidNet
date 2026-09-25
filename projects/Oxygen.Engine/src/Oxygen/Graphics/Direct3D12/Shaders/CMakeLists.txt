# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Shader baking (Task 14): build-time OXSL v1 library
# ------------------------------------------------------------------------------

set(SHADERBAKE_BUILD_ROOT "${CMAKE_BINARY_DIR}/shaderbake/$<CONFIG>")

# The ShaderBake tool target name (executable)
set(OXYGEN_D3D12_SHADERBAKE_TARGET "oxygen-graphics-direct3d12-shaderbake")

set(_OXYGEN_IS_ASAN_BUILD "${OXYGEN_WITH_ASAN}")

set(
  SHADER_LIBRARY_OUTPUT
  "${OXYGEN_PROJECT_SOURCE_DIR}/bin/Oxygen/$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${_OXYGEN_IS_ASAN_BUILD}>>,Debug,Release>/$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${_OXYGEN_IS_ASAN_BUILD}>>,dev,production>/shaders.bin"
)
set(
  SHADER_LIBRARY_OUTPUT_DIR
  "${OXYGEN_PROJECT_SOURCE_DIR}/bin/Oxygen/$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${_OXYGEN_IS_ASAN_BUILD}>>,Debug,Release>/$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${_OXYGEN_IS_ASAN_BUILD}>>,dev,production>"
)

include(RuntimeEnvironment)
oxygen_get_runtime_launcher(
  ${OXYGEN_D3D12_SHADERBAKE_TARGET}
  OXYGEN_SHADERBAKE_LAUNCHER
)

# Resolve the launcher list at generation time so paths containing spaces remain
# separate arguments on both Ninja and Visual Studio.
set(_shaderbake_script "${CMAKE_CURRENT_BINARY_DIR}/shaderbake-$<CONFIG>.cmake")
file(
  GENERATE
  OUTPUT
  "${_shaderbake_script}"
  CONTENT
    "set(OXYGEN_SHADERBAKE_RUNTIME [==[${OXYGEN_SHADERBAKE_LAUNCHER}]==])
set(OXYGEN_SHADERBAKE_EXECUTABLE [==[$<TARGET_FILE:${OXYGEN_D3D12_SHADERBAKE_TARGET}>]==])
set(OXYGEN_SHADERBAKE_WORKSPACE [==[${OXYGEN_PROJECT_SOURCE_DIR}]==])
set(OXYGEN_SHADERBAKE_CACHE [==[${SHADERBAKE_BUILD_ROOT}]==])
set(OXYGEN_SHADERBAKE_OUTPUT [==[${SHADER_LIBRARY_OUTPUT}]==])
set(OXYGEN_SHADERBAKE_OUTPUT_DIR [==[${SHADER_LIBRARY_OUTPUT_DIR}]==])
set(OXYGEN_SHADERBAKE_MODE [==[$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${_OXYGEN_IS_ASAN_BUILD}>>,dev,production>]==])
include([==[${OXYGEN_PROJECT_SOURCE_DIR}/cmake/RunShaderBake.cmake]==])
"
)

# ShaderBake owns source/include invalidation and missing-artifact recovery.
# An outer timestamp gate would hide changes to includes outside this directory.
add_custom_target(
  ${META_MODULE_TARGET}_shaders
  ALL
  COMMAND
    "${CMAKE_COMMAND}" -P "${_shaderbake_script}"
  BYPRODUCTS
    "${SHADER_LIBRARY_OUTPUT}"
  DEPENDS
    ${OXYGEN_D3D12_SHADERBAKE_TARGET}
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  VERBATIM
  COMMENT "Updating shaders.bin (OXSL v1) ($<CONFIG>)"
)

# Make the main module target depend on the baked shader library
add_dependencies(${META_MODULE_TARGET} ${META_MODULE_TARGET}_shaders)

if(OXYGEN_INSTALL)
  install(
    FILES
      "${SHADER_LIBRARY_OUTPUT}"
    DESTINATION "${OXYGEN_INSTALL_DATA}/shaders"
    COMPONENT Oxygen_data
  )
endif()
