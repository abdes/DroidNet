# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 4.2)
include("${OXYGEN_VERSION_CONFIG}")
include("${CMAKE_CURRENT_LIST_DIR}/SourceProvenance.cmake")
oxygen_read_source_provenance("${OXYGEN_PROJECT_SOURCE_DIR}" OXYGEN_SOURCE)
foreach(_part VERSION VERSION_MAJOR VERSION_MINOR VERSION_PATCH)
  set(META_${_part} "${OXYGEN_SOURCE_${_part}}")
endforeach()
set(META_VERSION_REVISION "${OXYGEN_SOURCE_REVISION}")
set(
  META_NAME_VERSION
  "${META_PROJECT_NAME} v${META_VERSION} (${OXYGEN_SOURCE_SHORT_REVISION})"
)
configure_file("${OXYGEN_VERSION_TEMPLATE}" "${OXYGEN_VERSION_HEADER}" @ONLY)
file(
  CONFIGURE
  OUTPUT "${OXYGEN_VERSION_CAPSULE}"
  CONTENT "${OXYGEN_SOURCE_JSON}"
  @ONLY
)
