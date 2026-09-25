# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

get_filename_component(_directory "${OXYGEN_DOXYGEN_WARNINGS}" DIRECTORY)
file(MAKE_DIRECTORY "${_directory}")
file(WRITE "${OXYGEN_DOXYGEN_WARNINGS}" "")
foreach(_file IN LISTS OXYGEN_DOXYGEN_WARNING_FILES)
  # Only the current configuration's registered modules belong in this report.
  # Their targets ran before dox; a missing log is an execution/wiring error.
  file(READ "${_file}" _warnings)
  file(APPEND "${OXYGEN_DOXYGEN_WARNINGS}" "${_warnings}")
endforeach()
message(STATUS "Oxygen documentation warnings: ${OXYGEN_DOXYGEN_WARNINGS}")
