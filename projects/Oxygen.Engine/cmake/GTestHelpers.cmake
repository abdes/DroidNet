# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

if(NOT TARGET GTest::gtest)
  find_package(GTest REQUIRED CONFIG)
endif()

include(GoogleTest)
include("${CMAKE_CURRENT_LIST_DIR}/TestRuntime.cmake")

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

  target_link_libraries(${program_name} PRIVATE ${x_DEPS})

  oxygen_configure_test_runtime(${program_name})
  gtest_discover_tests(
    ${program_name}
    DISCOVERY_TIMEOUT 60
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${program_name}>"
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
