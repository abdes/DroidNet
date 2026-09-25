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

# Python tools are provisioned during setup and verified by PythonTools.cmake.
# Builds only execute generators; they never install into an interpreter.
set(_tool_dir "${CMAKE_CURRENT_SOURCE_DIR}")

# Compile-only check: ensure the generated ABI and backend helper headers
# compile when included in a tiny TU. This target is kept under the tool so
# generator artifacts and checks remain local to the BindlessCodeGen package.
add_library(
  BindlessCodeGen_CompileCheck
  OBJECT
  tests/compile_check/compile_check.cpp
)

# Do not build the compile-check target independently as part of 'ALL'. Core's
# bindless generation umbrella depends on it. It can also be built explicitly.
set_target_properties(
  BindlessCodeGen_CompileCheck
  PROPERTIES
    EXCLUDE_FROM_ALL
      TRUE
)

# Core/Bindless wires this target to the generated-output target after defining
# it. That dependency includes the generation stamp, so
# direct compile-check builds also wait for current generated headers.

# Add include path to the Bindless output directory so the generated headers
# can be found when this TU is compiled. The generator writes headers into
# src/Oxygen/Core/Bindless.
target_include_directories(
  BindlessCodeGen_CompileCheck
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/Oxygen/Core/Bindless
)

if(TARGET fmt::fmt-header-only)
  target_link_libraries(
    BindlessCodeGen_CompileCheck
    PRIVATE
      fmt::fmt-header-only
  )
elseif(TARGET fmt::fmt)
  target_link_libraries(BindlessCodeGen_CompileCheck PRIVATE fmt::fmt)
endif()

# Add CTest integration for running the Python unit tests
if(OXYGEN_BUILD_TESTS)
  add_test(
    NAME BindlessCodeGen_UnitTests
    COMMAND
      ${Python3_EXECUTABLE} -m pytest tests/ -v
    WORKING_DIRECTORY "${_tool_dir}"
  )

  # Setup provisions pytest; tests consume the same verified interpreter.
  set_tests_properties(
    BindlessCodeGen_UnitTests
    PROPERTIES
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
