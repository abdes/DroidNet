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

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(DescriptorAllocator SOURCES "DescriptorAllocator_test.cpp")
m_gtest_program(SmokeTest SOURCES "Headless_Smoke_test.cpp")
