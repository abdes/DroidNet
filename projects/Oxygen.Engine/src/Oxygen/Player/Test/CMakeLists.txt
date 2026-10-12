# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

find_package(GTest REQUIRED CONFIG)

include(GTestHelpers)

gtest_program(
  "${META_MODULE_NAME}.Tests"
  SOURCES
    "LaunchFile_test.cpp"
    "PipeChannel_test.cpp"
    "PlayerProtocol_test.cpp"
  DEPS
    oxygen-player-core
    oxygen::testing
)
