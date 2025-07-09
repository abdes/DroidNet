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
target_compile_options(
  ${OXYGEN_SERIO_LINK_TEST_TARGET}
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
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

m_gtest_program(
  Serio
  SOURCES
    "FileStream_test.cpp"
    "MemoryStream_test.cpp"
    "Reader_test.cpp"
    "Writer_test.cpp"
)

m_gtest_program(SerioFullCycle SOURCES "Integration_test.cpp")
