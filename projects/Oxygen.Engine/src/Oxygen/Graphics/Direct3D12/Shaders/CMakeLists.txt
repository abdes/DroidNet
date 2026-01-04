# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Shader compilation
# ------------------------------------------------------------------------------

set(SHADER_FullScreenTriangle_TYPES "VS;PS")
set(SHADER_DepthPrePass_TYPES "VS;PS")
set(SHADER_LightCulling_TYPES "CS")
set(SHADER_ImGui_TYPES "VS;PS")

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
  dxc_enable_advanced_features
  "-enable-16bit-types"
  "-HV 2021"
)
set(
  dxc_vs_shader_model
  "-T"
  "vs_6_6"
)
set(
  dxc_ps_shader_model
  "-T"
  "ps_6_6"
)
set(
  dxc_cs_shader_model
  "-T"
  "cs_6_6"
)

# Define default entry points
set(DEFAULT_VS_ENTRY_POINT "VS")
set(DEFAULT_PS_ENTRY_POINT "PS")
set(DEFAULT_CS_ENTRY_POINT "CS")

# Find all HLSL files in the Shaders subdirectory
file(GLOB HLSL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl")

# Shared include files (HLSL headers)
file(GLOB HLSLI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsli")

# Create a custom target for compiling shaders
add_custom_target(
  ${META_MODULE_TARGET}_shaders
  ALL
  COMMENT "Building HLSL shaders for ${META_MODULE_TARGET}"
)

# Common DXC options (excluding model, entry point, output file, and PDB path)
set(
  DXC_COMMON_OPTIONS
  ${dxc_enable_advanced_features}
  -I
  "${PROJECT_SOURCE_DIR}/src/Oxygen"
  -I
  "${CMAKE_CURRENT_SOURCE_DIR}"
  $<$<CONFIG:Debug>:${dxc_debug_symbol}>
  $<$<CONFIG:Debug>:${dxc_optimization_debug}>
  $<$<NOT:$<CONFIG:Debug>>:${dxc_optimization_release}>
)

# Process each HLSL file
foreach(HLSL_FILE ${HLSL_FILES})
  get_filename_component(shader_name ${HLSL_FILE} NAME_WE)

  set(shader_types_var_name "SHADER_${shader_name}_TYPES")
  if(NOT DEFINED ${shader_types_var_name})
    message(
      WARNING
      "Shader types for ${shader_name} "
      "(variable ${shader_types_var_name}) are not defined. "
      "Skipping this shader. Define ${shader_types_var_name} "
      "to specify types (e.g., \"VS;PS\", \"CS\")."
    )
    continue() # Skip to the next HLSL_FILE
  else()
    set(CURRENT_SHADER_TYPES ${${shader_types_var_name}})
  endif()

  set(SHADER_SPECIFIC_OUTPUTS "")
  set(SHADER_SPECIFIC_COMMANDS "")

  # Vertex Shader
  if("VS" IN_LIST CURRENT_SHADER_TYPES)
    set(VS_ENTRY_POINT ${DEFAULT_VS_ENTRY_POINT}) # Allow override: SHADER_${shader_name}_VS_ENTRY_POINT
    set(VS_SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${shader_name}_vs.cso")
    list(APPEND SHADER_SPECIFIC_OUTPUTS ${VS_SHADER_OUTPUT})

    set(VS_COMPILE_COMMAND_ARGS "")
    list(
      APPEND
      VS_COMPILE_COMMAND_ARGS
      ${dxc_vs_shader_model}
      -E
      ${VS_ENTRY_POINT}
    )
    list(APPEND VS_COMPILE_COMMAND_ARGS ${DXC_COMMON_OPTIONS})
    list(
      APPEND
      VS_COMPILE_COMMAND_ARGS
      $<$<CONFIG:Debug>:-Fd>
      $<$<CONFIG:Debug>:${SHADER_OUTPUT_DIR}/${shader_name}_vs.pdb>
    )
    list(
      APPEND
      VS_COMPILE_COMMAND_ARGS
      -Fo
      ${VS_SHADER_OUTPUT}
      ${HLSL_FILE}
    )
    list(
      APPEND
      SHADER_SPECIFIC_COMMANDS
      COMMAND
      ${dxc_exe}
      ${VS_COMPILE_COMMAND_ARGS}
    )
  endif()

  # Pixel Shader
  if("PS" IN_LIST CURRENT_SHADER_TYPES)
    set(PS_ENTRY_POINT ${DEFAULT_PS_ENTRY_POINT}) # Allow override: SHADER_${shader_name}_PS_ENTRY_POINT
    set(PS_SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${shader_name}_ps.cso")
    list(APPEND SHADER_SPECIFIC_OUTPUTS ${PS_SHADER_OUTPUT})

    set(PS_COMPILE_COMMAND_ARGS "")
    list(
      APPEND
      PS_COMPILE_COMMAND_ARGS
      ${dxc_ps_shader_model}
      -E
      ${PS_ENTRY_POINT}
    )
    list(APPEND PS_COMPILE_COMMAND_ARGS ${DXC_COMMON_OPTIONS})
    list(
      APPEND
      PS_COMPILE_COMMAND_ARGS
      $<$<CONFIG:Debug>:-Fd>
      $<$<CONFIG:Debug>:${SHADER_OUTPUT_DIR}/${shader_name}_ps.pdb>
    )
    list(
      APPEND
      PS_COMPILE_COMMAND_ARGS
      -Fo
      ${PS_SHADER_OUTPUT}
      ${HLSL_FILE}
    )
    list(
      APPEND
      SHADER_SPECIFIC_COMMANDS
      COMMAND
      ${dxc_exe}
      ${PS_COMPILE_COMMAND_ARGS}
    )
  endif()

  # Compute Shader
  if("CS" IN_LIST CURRENT_SHADER_TYPES)
    set(CS_ENTRY_POINT ${DEFAULT_CS_ENTRY_POINT}) # Allow override: SHADER_${shader_name}_CS_ENTRY_POINT
    set(CS_SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${shader_name}_cs.cso")
    list(APPEND SHADER_SPECIFIC_OUTPUTS ${CS_SHADER_OUTPUT})

    set(CS_COMPILE_COMMAND_ARGS "")
    list(
      APPEND
      CS_COMPILE_COMMAND_ARGS
      ${dxc_cs_shader_model}
      -E
      ${CS_ENTRY_POINT}
    )
    list(APPEND CS_COMPILE_COMMAND_ARGS ${DXC_COMMON_OPTIONS})
    list(
      APPEND
      CS_COMPILE_COMMAND_ARGS
      $<$<CONFIG:Debug>:-Fd>
      $<$<CONFIG:Debug>:${SHADER_OUTPUT_DIR}/${shader_name}_cs.pdb>
    )
    list(
      APPEND
      CS_COMPILE_COMMAND_ARGS
      -Fo
      ${CS_SHADER_OUTPUT}
      ${HLSL_FILE}
    )
    list(
      APPEND
      SHADER_SPECIFIC_COMMANDS
      COMMAND
      ${dxc_exe}
      ${CS_COMPILE_COMMAND_ARGS}
    )
  endif()

  if(SHADER_SPECIFIC_OUTPUTS) # Check if list is not empty
    add_custom_command(
      OUTPUT
        ${SHADER_SPECIFIC_OUTPUTS}
      COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${SHADER_OUTPUT_DIR}" # Ensures $CONFIG specific dir exists
        ${SHADER_SPECIFIC_COMMANDS}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      VERBATIM
      COMMENT
        "Compiling shader: ${shader_name}.hlsl for types (${CURRENT_SHADER_TYPES}) ($<CONFIG> mode)"
      DEPENDS
        "${HLSL_FILE}"
        ${HLSLI_FILES}
      MAIN_DEPENDENCY
        "${HLSL_FILE}" # Important for some generators like Ninja and Visual Studio
    )

    add_custom_target(
      ${shader_name}_shader
      ALL
      DEPENDS
        ${SHADER_SPECIFIC_OUTPUTS}
    )
    add_dependencies(${META_MODULE_TARGET}_shaders ${shader_name}_shader)
  elseif(CURRENT_SHADER_TYPES) # If types were specified but none matched VS/PS/CS
    message(
      WARNING
      "Shader ${shader_name}.hlsl: Specified types (${CURRENT_SHADER_TYPES}) did not result in any compilation commands."
    )
  endif()
endforeach()

# Make the main module target depend on all shaders
add_dependencies(${META_MODULE_TARGET} ${META_MODULE_TARGET}_shaders)
