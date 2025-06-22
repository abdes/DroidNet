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

set(TEST_NAME "GlobalInit.FromCompLib")
m_gtest_program(${TEST_NAME} SOURCES "InitFromCompLib_test.cpp")
