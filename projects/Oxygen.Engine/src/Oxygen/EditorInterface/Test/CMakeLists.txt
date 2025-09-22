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

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(Linked SOURCES "Linked_test.cpp")
m_gtest_program(Loaded SOURCES "Loaded_test.cpp")
