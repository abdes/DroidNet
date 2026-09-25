# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
execute_process(
  COMMAND
    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/CheckBuildPython.py"
    "${OXYGEN_PROJECT_SOURCE_DIR}" "${OXYGEN_PYTHON_LOCKFILE}"
    "${OXYGEN_BUILD_TESTS}"
  RESULT_VARIABLE _python_status
  ERROR_VARIABLE _python_error
  OUTPUT_VARIABLE _python_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _python_status EQUAL 0)
  message(FATAL_ERROR "${_python_error}")
endif()
string(JSON OXYGEN_PYTHON_LOCKFILE GET "${_python_result}" lockfile)
string(JSON _python_input_count LENGTH "${_python_result}" inputs)
math(EXPR _python_input_last "${_python_input_count} - 1")
foreach(_index RANGE ${_python_input_last})
  string(
    JSON
    _python_input
    GET "${_python_result}"
    inputs
    ${_index}
  )
  set_property(
    DIRECTORY
    APPEND
    PROPERTY
      CMAKE_CONFIGURE_DEPENDS
        "${_python_input}"
  )
endforeach()
