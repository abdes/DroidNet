# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ==============================================================================
# Build instructions
# ==============================================================================

oxygen_cooker_test(
  PakDump
  FUNCTIONAL
  DEPS
    oxygen-cooker-pakdump-core
    oxygen::content
    oxygen::data
  SOURCES
    "PakDump_functional_test.cpp"
)
