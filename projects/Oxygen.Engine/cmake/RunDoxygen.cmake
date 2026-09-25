# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

foreach(
  _required
  IN
  ITEMS
    OXYGEN_DOXYGEN_EXECUTABLE
    OXYGEN_DOXYGEN_CONFIG
    OXYGEN_DOXYGEN_WARNINGS
)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required.")
  endif()
endforeach()
get_filename_component(_directory "${OXYGEN_DOXYGEN_WARNINGS}" DIRECTORY)
file(MAKE_DIRECTORY "${_directory}")
# Do not report stale warnings if Doxygen fails before opening its log.
file(WRITE "${OXYGEN_DOXYGEN_WARNINGS}" "")
execute_process(
  COMMAND
    "${OXYGEN_DOXYGEN_EXECUTABLE}" "${OXYGEN_DOXYGEN_CONFIG}"
  RESULT_VARIABLE _result
  ERROR_VARIABLE _diagnostics
)
# Configuration diagnostics can be emitted before Doxygen opens WARN_LOGFILE.
# Keep them in the module report as well as displaying them with other warnings.
if(NOT _diagnostics STREQUAL "")
  file(APPEND "${OXYGEN_DOXYGEN_WARNINGS}" "${_diagnostics}")
endif()
file(READ "${OXYGEN_DOXYGEN_WARNINGS}" _warnings)
if(NOT _warnings STREQUAL "")
  # Also show warnings for a directly requested module target. The aggregate
  # collector writes the combined report without printing them a second time.
  message("${_warnings}")
endif()
if(NOT "${_result}" MATCHES "^[0-9]+$")
  message(FATAL_ERROR "Doxygen could not run: ${_result}")
endif()
cmake_language(EXIT "${_result}")
