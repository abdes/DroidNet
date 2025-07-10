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

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Parser
  SOURCES
    "TestHelpers.h"
    "IdentifyCommandState_test.cpp"
    "InitialState_test.cpp"
    "OptionValueState_test.cpp"
    "ParseShortOptionState_test.cpp"
    "ParseLongOptionState_test.cpp"
    "ParseOptionsState_test.cpp"
)

m_gtest_program(Tokenizer SOURCES "Tokenizer_test.cpp")
