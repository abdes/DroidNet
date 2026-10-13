# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Commands
  SOURCES
    "Params_test.cpp"
    "CommandRegistry_test.cpp"
)
