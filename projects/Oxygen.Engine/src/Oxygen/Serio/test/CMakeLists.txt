# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

set(OXYGEN_SERIO_LINK_TEST_TARGET "Oxygen.Serio.LinkTest")
add_executable(${OXYGEN_SERIO_LINK_TEST_TARGET} "Link_test.cpp")
set_target_properties(
  ${OXYGEN_SERIO_LINK_TEST_TARGET}
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(
  ${OXYGEN_SERIO_LINK_TEST_TARGET}
  PRIVATE
    oxygen::base
    oxygen::serio
)
add_test(NAME "Oxygen.Serio Link Test" COMMAND ${OXYGEN_SERIO_LINK_TEST_TARGET})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

# Declares one Serio test executable of a single tier (see README.md):
# Oxygen.Serio.<Unit|Functional>.Tests, labelled "serio;<tier>".
function(oxygen_serio_test tier)
  cmake_parse_arguments(x "" "" "SOURCES" ${ARGN})
  string(TOLOWER "${tier}" _label)
  set(_name "Oxygen.Serio.${tier}.Tests")
  gtest_program(
    ${_name}
    SOURCES
      ${x_SOURCES}
    DEPS
      oxygen::serio
      oxygen::serio::testing
      oxygen::testing
    LABELS
      serio
      ${_label}
  )
  target_include_directories(${_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

oxygen_serio_test(
  Unit
  SOURCES
    "Conformance/StreamConformance.h"
    "Support/DeviceFactories.h"
    "Support/StreamFactories.h"
    "AnyDevice_test.cpp"
    "ByteBuffer_test.cpp"
    "ByteReader_test.cpp"
    "CopyRegion_test.cpp"
    "CountingSink_test.cpp"
    "Error_test.cpp"
    "FaultInjectingDevice_test.cpp"
    "Integrity_test.cpp"
    "Integration_test.cpp"
    "MemoryFile_test.cpp"
    "MemoryStream_test.cpp"
    "Reader_test.cpp"
    "Region_test.cpp"
    "RegionReader_test.cpp"
    "RegionStream_test.cpp"
    "RegionWriter_test.cpp"
    "Serialization_test.cpp"
    "StreamConformance_test.cpp"
    "Types_test.cpp"
    "Writer_test.cpp"
)

oxygen_serio_test(
  Functional
  SOURCES
    "Conformance/AsyncDeviceConformance.h"
    "Conformance/DeviceConformance.h"
    "Conformance/StreamConformance.h"
    "Support/DeviceFactories.h"
    "Support/StreamFactories.h"
    "AsyncDeviceConformance_functional_test.cpp"
    "AsyncFile_functional_test.cpp"
    "AtomicFile_functional_test.cpp"
    "DeviceConformance_functional_test.cpp"
    "FileLock_functional_test.cpp"
    "Integrity_functional_test.cpp"
    "MappedFile_functional_test.cpp"
    "NativeFile_functional_test.cpp"
    "StreamConformance_functional_test.cpp"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(Oxygen.Serio.FileLockProbe "FileLockProbe.cpp")
target_link_libraries(Oxygen.Serio.FileLockProbe PRIVATE oxygen::serio)
set_target_properties(
  Oxygen.Serio.FileLockProbe
  PROPERTIES
    FOLDER
      "Testing"
)
add_test(
  NAME "Oxygen.Serio.FileLock.CrossProcess"
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/file_lock_process_test.py"
    "$<TARGET_FILE:Oxygen.Serio.FileLockProbe>"
)
set_tests_properties(
  "Oxygen.Serio.FileLock.CrossProcess"
  PROPERTIES
    TIMEOUT
      30
    LABELS
      "serio;functional"
)

# -- Aggregate -----------------------------------------------------------------

# Builds every Serio test program this platform defines; the list is collected
# so a new program needs no edit here.
get_directory_property(serio_test_targets BUILDSYSTEM_TARGETS)
add_custom_target("Oxygen.Serio.AllTests" DEPENDS ${serio_test_targets})
set_target_properties(
  "Oxygen.Serio.AllTests"
  PROPERTIES
    FOLDER
      "Testing"
)
