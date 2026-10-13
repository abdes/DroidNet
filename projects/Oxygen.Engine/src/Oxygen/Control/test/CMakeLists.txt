# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Control
  SOURCES
    "CommandDispatcher_test.cpp"
    "ControlServer_test.cpp"
    "EventLog_test.cpp"
)
target_link_libraries(
  Oxygen.Control.Control.Tests
  PRIVATE
    oxygen::console
    oxygen::engine
    asio::asio
)
