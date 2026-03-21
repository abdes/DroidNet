# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

include(GTestHelpers)

m_gtest_program(
  SceneLoaderServicePhase4
  SOURCES
    "SceneLoaderService_phase4_test.cpp"
)

m_gtest_program(
  ConsoleDefaults
  SOURCES
    "DemoShellConsoleDefaults_test.cpp"
)
