# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

add_executable("Oxygen.Graphics.Common.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Graphics.Common.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Graphics.Common.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Graphics.Common.LinkTest"
  PRIVATE
    oxygen::graphics-common
)
add_test(
  NAME "Oxygen.Graphics.Common Link Test"
  COMMAND
    "Oxygen.Graphics.Common.LinkTest"
)

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(ShaderByteCode SOURCES "ShaderByteCode_test.cpp")
