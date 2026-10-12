# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Keeps clangd's per-configuration compilation databases in step with CMake's
# export. Explicit configures split them (tools/build-tree.ps1, the VS Code
# post-configure task), but a build that regenerates the tree itself, such as
# the glob re-check after a source file is added, rewrites
# compile_commands.json without that step, leaving new files without a compile
# command for clangd and oxytidy.

include_guard(GLOBAL)

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS OR NOT Python3_EXECUTABLE)
  return()
endif()
if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
  return()
endif()

set(
  _oxygen_clangd_script
  "${OXYGEN_PROJECT_SOURCE_DIR}/.vscode/prepare_clangd.py"
)
set(_oxygen_clangd_stamp "${CMAKE_BINARY_DIR}/clangd/$<CONFIG>/.prepared")

add_custom_command(
  OUTPUT
    "${_oxygen_clangd_stamp}"
  COMMAND
    "${Python3_EXECUTABLE}" "${_oxygen_clangd_script}" --build-dir
    "${CMAKE_BINARY_DIR}"
  COMMAND
    "${CMAKE_COMMAND}" -E touch "${_oxygen_clangd_stamp}"
  DEPENDS
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${_oxygen_clangd_script}"
  COMMENT "Refreshing clangd compilation databases"
  VERBATIM
)
add_custom_target(oxygen-clangd-databases ALL DEPENDS "${_oxygen_clangd_stamp}")
