# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Invoked by a generated per-target/configuration launcher. Build the call using
# bracket arguments to preserve empty arguments, semicolons and Windows paths.
# Choose a delimiter that cannot occur in the argument rather than interpreting
# command-line data as CMake source.
function(_oxygen_quote_argument value output)
  set(_equals "==")
  while("${value}" MATCHES "\\]${_equals}\\]")
    string(APPEND _equals "=")
  endwhile()
  set(${output} "[${_equals}[${value}]${_equals}]" PARENT_SCOPE)
endfunction()

set(_command "execute_process(COMMAND")
foreach(_argument IN LISTS OXYGEN_RUNTIME_COMMAND)
  _oxygen_quote_argument("${_argument}" _quoted)
  string(APPEND _command " ${_quoted}")
endforeach()
# cmake -P <script> -- <command> <arguments...>
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_index RANGE 4 ${_last})
  _oxygen_quote_argument("${CMAKE_ARGV${_index}}" _quoted)
  string(APPEND _command " ${_quoted}")
endforeach()
string(APPEND _command " RESULT_VARIABLE _result)")
cmake_language(EVAL CODE "${_command}")
if(NOT "${_result}" MATCHES "^[0-9]+$")
  message(FATAL_ERROR "Test process failed: ${_result}")
endif()
cmake_language(EXIT "${_result}")
