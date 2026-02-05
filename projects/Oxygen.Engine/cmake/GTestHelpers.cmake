# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

if(NOT GTest::gtest)
  find_package(GTest REQUIRED CONFIG)
endif()

include(GoogleTest)

# ------------------------------------------------------------------------------
# Build Helpers to simplify test target creation.
# ------------------------------------------------------------------------------

function(gtest_program program_name)
  set(options)
  set(one_value_args)
  set(
    multi_value_args
    SOURCES
    DEPS
  )
  cmake_parse_arguments(
    x
    "${options}"
    "${one_value_args}"
    "${multi_value_args}"
    ${ARGN}
  )

  # Define the executable
  add_executable(${program_name} ${x_SOURCES})
  set_target_properties(
    ${program_name}
    PROPERTIES
      FOLDER
        "Testing"
  )

  target_compile_options(${program_name} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})

  if(OXYGEN_WITH_COVERAGE)
    target_compile_options(${program_name} PRIVATE "--coverage")
    target_link_options(${program_name} PRIVATE "--coverage")
  endif()

  target_link_libraries(${program_name} PRIVATE ${x_DEPS})

  # Set the DLL path relative to the build directory
  set(RUNTIME_DLL_PATH "${CMAKE_BINARY_DIR}/../install/$<CONFIG>")
  # For all test targets, set the PATH environment
  gtest_discover_tests(
    ${program_name}
    DISCOVERY_TIMEOUT 60
    WORKING_DIRECTORY ${RUNTIME_DLL_PATH}
  )

  # Define the test
  add_test(NAME ${program_name} COMMAND ${program_name})
endfunction()

function(m_gtest_program program_name)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs SOURCES)

  cmake_parse_arguments(
    x
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
  )
  gtest_program(
    "${META_MODULE_NAME}.${program_name}.Tests"
    SOURCES
      ${x_SOURCES}
    DEPS
      ${META_MODULE_TARGET}
      oxygen::testing
  )
  source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX ${program_name}
    FILES
      ${x_SOURCES}
  )
endfunction()
