# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

if(NOT Catch2::Catch2)
  find_package(Catch2 REQUIRED CONFIG)
endif()

include(Catch)

# ------------------------------------------------------------------------------
# Build Helpers to simplify test target creation.
# ------------------------------------------------------------------------------

function(catch2_program program_name)
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
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${x_SOURCES})

  target_compile_options(${program_name} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})

  if(OXYGEN_WITH_COVERAGE)
    target_compile_options(${program_name} PRIVATE "--coverage")
    target_link_options(${program_name} PRIVATE "--coverage")
  endif()

  target_link_libraries(${program_name} PRIVATE ${x_DEPS})
  catch_discover_tests(${program_name})

  # Define the test
  add_test(NAME ${program_name} COMMAND ${program_name})
endfunction()

function(m_catch2_program program_name)
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
  catch2_program(
    "${META_MODULE_NAME}.${program_name}.Tests"
    SOURCES
      ${x_SOURCES}
    DEPS
      ${META_MODULE_TARGET}
      oxygen::base::catch2
  )
endfunction()
