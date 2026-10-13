# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

include(GTestHelpers)

# ==============================================================================
# Build instructions
# ==============================================================================

set(TEST_NAME "GlobalInit.FromMain")
m_gtest_program(
  ${TEST_NAME}
  SOURCES
    "InitFromMain_test.cpp"
    "testing_cs_init.cpp"
)

# NOTE: MUST tell clang and gcc to export symbols even from the executable.
# Needed so that the type system can find the `InitializeTypeRegistry`
# function defined in the executable.
target_link_options(
  "${META_MODULE_NAME}.${TEST_NAME}.Tests"
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wl,--export-dynamic>
)
