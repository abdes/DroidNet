# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)
find_package(dxc CONFIG REQUIRED)

# Keep build-machine executables separate from the host API/runtime. Conan
# supplies these directories from the corresponding dependency contexts.
if(NOT OXYGEN_DXC_TOOL_BINDIRS OR NOT OXYGEN_DXC_RUNTIME_BINDIRS)
  message(
    FATAL_ERROR
    "DXC requires Oxygen's Conan-generated toolchain. Run conan install for this build tree."
  )
endif()
find_program(
  OXYGEN_DXC_EXECUTABLE
  NAMES
    dxc
  PATHS
    ${OXYGEN_DXC_TOOL_BINDIRS}
  NO_DEFAULT_PATH
  NO_CACHE
  REQUIRED
)
find_file(
  OXYGEN_DXCOMPILER_DLL
  NAMES
    dxcompiler.dll
  PATHS
    ${OXYGEN_DXC_RUNTIME_BINDIRS}
  NO_DEFAULT_PATH
  NO_CACHE
  REQUIRED
)
find_file(
  OXYGEN_DXIL_DLL
  NAMES
    dxil.dll
  PATHS
    ${OXYGEN_DXC_RUNTIME_BINDIRS}
  NO_DEFAULT_PATH
  NO_CACHE
  REQUIRED
)

function(oxygen_stage_dxc_runtime target)
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "${OXYGEN_DXCOMPILER_DLL}"
      "${OXYGEN_DXIL_DLL}" "$<TARGET_FILE_DIR:${target}>"
    VERBATIM
  )
endfunction()

# Compile a compute test probe that must stay out of shaders.bin.
#
#   oxygen_add_dxc_compute_probe(<name>
#     SOURCE <hlsl> PROFILE <cs_6_x> CONSUMER <target> DEFINITION <macro>
#     [CAMERA_VIEW_ENTRY])
#
# Builds `<name>.dxil` next to the calling directory's outputs, with a DXC
# depfile for its includes, and gives <consumer> the DXIL path as <macro>.
# CAMERA_VIEW_ENTRY compiles it as a camera view entry, like a catalog entry
# marked `.view_binding=kCamera`.
function(oxygen_add_dxc_compute_probe name)
  cmake_parse_arguments(
    PARSE_ARGV
    1
    arg
    "CAMERA_VIEW_ENTRY"
    "SOURCE;PROFILE;CONSUMER;DEFINITION"
    ""
  )
  foreach(required SOURCE PROFILE CONSUMER DEFINITION)
    if(NOT arg_${required})
      message(
        FATAL_ERROR
        "oxygen_add_dxc_compute_probe(${name}) needs ${required}"
      )
    endif()
  endforeach()
  cmake_path(
    ABSOLUTE_PATH
    arg_SOURCE
    BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
  set(output "${output_dir}/${name}.dxil")
  set(
    flags
    -T
    ${arg_PROFILE}
    -E
    CS
    -O3
    -HV
    2021
    -I
    "${PROJECT_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/Shaders"
    -I
    "${PROJECT_SOURCE_DIR}/src/Oxygen"
  )
  if(arg_CAMERA_VIEW_ENTRY)
    list(
      APPEND
      flags
      -D
      OXYGEN_CAMERA_VIEW_ENTRY=1
    )
  endif()
  add_custom_command(
    OUTPUT
      "${output}"
    COMMAND
      ${CMAKE_COMMAND} -E make_directory "${output_dir}"
    COMMAND
      "${OXYGEN_DXC_EXECUTABLE}" ${flags} -Fo "${output}" "${arg_SOURCE}"
    # DXC dependency generation is a separate preprocessing action; -MD does
    # not compile DXIL, even when -Fo is supplied.
    COMMAND
      "${OXYGEN_DXC_EXECUTABLE}" ${flags} -MD -MF "${output}.d" -Fo "${output}"
      "${arg_SOURCE}"
    DEPENDS
      "${OXYGEN_DXC_EXECUTABLE}"
      "${arg_SOURCE}"
    DEPFILE "${output}.d"
    VERBATIM
  )
  add_custom_target(${name} DEPENDS "${output}")
  add_dependencies(${arg_CONSUMER} ${name})
  target_compile_definitions(
    ${arg_CONSUMER}
    PRIVATE
      ${arg_DEFINITION}="${output}"
  )
endfunction()
