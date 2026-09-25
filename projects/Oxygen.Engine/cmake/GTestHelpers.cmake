# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

if(NOT TARGET GTest::gtest)
  find_package(GTest REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/TestRuntime.cmake")

# ------------------------------------------------------------------------------
# Build Helpers to simplify test target creation.
# ------------------------------------------------------------------------------

function(gtest_program program_name)
  set(
    options
    GPU
    NO_TEST
  )
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
  # Benchmarks reuse the executable setup without joining the correctness suite.
  # Register the entire suite once; individual cases remain selectable directly
  # through the executable's --gtest_filter option.
  if(NOT x_NO_TEST)
    add_test(NAME ${program_name} COMMAND ${program_name})
    if(x_GPU)
      set_tests_properties(
        ${program_name}
        PROPERTIES
          RESOURCE_LOCK
            oxygen_gpu
      )
    endif()
  endif()
endfunction()

function(m_gtest_program program_name)
  set(options GPU)
  set(oneValueArgs)
  set(multiValueArgs SOURCES)

  cmake_parse_arguments(
    x
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
  )
  set(_options)
  if(x_GPU)
    list(APPEND _options GPU)
  endif()
  gtest_program(
    "${META_MODULE_NAME}.${program_name}.Tests"
    ${_options}
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
