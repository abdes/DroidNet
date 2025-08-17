# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# BindlessCodeGen Tool Configuration
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ==============================================================================
# Tool Installation and Build Instructions
# ==============================================================================

set(_tool_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_stamp "${CMAKE_BINARY_DIR}/bindless_codegen_editable_install.stamp")
set(_pyproject "${_tool_dir}/pyproject.toml")

# ------------------------------------------------------------------------------
# Editable Install Target
# ------------------------------------------------------------------------------

add_custom_command(
  OUTPUT
    "${_stamp}"
  COMMAND
    ${CMAKE_COMMAND} -E echo "[BindlessCodeGen] Editable install starting"
  COMMAND
    ${CMAKE_COMMAND} -E env PIP_DISABLE_PIP_VERSION_CHECK=1
    ${Python3_EXECUTABLE} -m pip install -r requirements.txt
  COMMAND
    ${CMAKE_COMMAND} -E env PIP_DISABLE_PIP_VERSION_CHECK=1
    ${Python3_EXECUTABLE} -m pip install -e .
  COMMAND
    ${CMAKE_COMMAND} -E touch "${_stamp}"
  WORKING_DIRECTORY "${_tool_dir}"
  DEPENDS
    "${_pyproject}"
  COMMENT "Installing BindlessCodeGen (editable)"
  VERBATIM
)

add_custom_target(bindless_codegen_editable_install ALL DEPENDS "${_stamp}")
set_target_properties(
  bindless_codegen_editable_install
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      "Editable Install of BindlessCodeGen"
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

# Add CTest integration for running the Python unit tests
if(BUILD_TESTING)
  add_test(
    NAME BindlessCodeGen_UnitTests
    COMMAND
      ${Python3_EXECUTABLE} -m pytest tests/ -v
    WORKING_DIRECTORY "${_tool_dir}"
  )

  # Ensure the tool is installed before running tests
  set_tests_properties(
    BindlessCodeGen_UnitTests
    PROPERTIES
      DEPENDS
        bindless_codegen_editable_install
      LABELS
        "Tools;BindlessCodeGen;Python"
  )
endif()

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------

# If example CMake lists are present, include them so examples are available in the
# top-level build tree and CTest. This matches the pattern used by other Tools
# directories in the repository.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
  add_subdirectory("examples")
endif()
