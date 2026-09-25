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
