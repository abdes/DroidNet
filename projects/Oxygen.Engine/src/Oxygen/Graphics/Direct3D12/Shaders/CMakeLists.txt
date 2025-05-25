# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Shader compilation
# ------------------------------------------------------------------------------

set(dxc_exe "${dxc_bin_path}/dxc.exe")
message(STATUS "Using DXC Compiler: ${dxc_exe}")

# Define shader output directory
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>")

# Ensure the base shader directories exist
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

# Define DXC compilation options with generator expressions using lists
# Use lists instead of strings for command line options
set(dxc_optimization_debug "-Od")
set(dxc_optimization_release "-O3")
set(dxc_debug_symbol "-Zi")
set(
  dxc_vs_shader_model
  "-T"
  "vs_6_8"
)
set(
  dxc_ps_shader_model
  "-T"
  "ps_6_8"
)

# Find all HLSL files in the Shaders subdirectory
file(GLOB HLSL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl")

# Create a custom target for compiling shaders
add_custom_target(
  ${META_MODULE_TARGET}_shaders
  ALL
  COMMENT "Building HLSL shaders for ${META_MODULE_TARGET}"
)

# Process each HLSL file
foreach(HLSL_FILE ${HLSL_FILES})
  get_filename_component(shader_name ${HLSL_FILE} NAME_WE)

  # Set entry points based on shader file
  set(VS_ENTRY_POINT "VS")
  set(PS_ENTRY_POINT "PS")

  # Define output shader files
  set(VS_SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${shader_name}_vs.cso")
  set(PS_SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${shader_name}_ps.cso")

  # Create a custom target for this shader that will compile both vertex and pixel shaders
  add_custom_command(
    OUTPUT
      ${VS_SHADER_OUTPUT}
      ${PS_SHADER_OUTPUT}
    # Create output directory
    COMMAND
      ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>"
    # Compile vertex shader with config-specific options
    COMMAND
      ${dxc_exe} ${dxc_vs_shader_model} -E ${VS_ENTRY_POINT}
      $<$<CONFIG:Debug>:${dxc_debug_symbol}>
      $<$<CONFIG:Debug>:${dxc_optimization_debug}>
      $<$<NOT:$<CONFIG:Debug>>:${dxc_optimization_release}>
      $<$<CONFIG:Debug>:-Fd>
      $<$<CONFIG:Debug>:${SHADER_OUTPUT_DIR}/${shader_name}_vs.pdb> -Fo
      ${SHADER_OUTPUT_DIR}/${shader_name}_vs.cso ${HLSL_FILE}
    # Compile pixel shader with config-specific options
    COMMAND
      ${dxc_exe} ${dxc_ps_shader_model} -E ${PS_ENTRY_POINT}
      $<$<CONFIG:Debug>:${dxc_debug_symbol}>
      $<$<CONFIG:Debug>:${dxc_optimization_debug}>
      $<$<NOT:$<CONFIG:Debug>>:${dxc_optimization_release}>
      $<$<CONFIG:Debug>:-Fd>
      $<$<CONFIG:Debug>:${SHADER_OUTPUT_DIR}/${shader_name}_ps.pdb> -Fo
      ${SHADER_OUTPUT_DIR}/${shader_name}_ps.cso ${HLSL_FILE}
    # Set working directory and verbosity
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    # Display what we're building
    COMMENT "Compiling shader: ${shader_name}.hlsl ($<CONFIG> mode)"
    # Set this shader file as a dependency to ensure rebuilding when it changes
    DEPENDS
      "${HLSL_FILE}"
  )

  add_custom_target(
    ${shader_name}_shader
    ALL
    DEPENDS
      ${VS_SHADER_OUTPUT}
      ${PS_SHADER_OUTPUT}
  )
  add_dependencies(${META_MODULE_TARGET}_shaders ${shader_name}_shader)
endforeach()

# Make the main module target depend on all shaders
add_dependencies(${META_MODULE_TARGET} ${META_MODULE_TARGET}_shaders)
