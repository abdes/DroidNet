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

m_gtest_program(ConsoleDefaults SOURCES "DemoShellConsoleDefaults_test.cpp")

m_gtest_program(DefaultSceneLighting SOURCES "DefaultSceneLighting_test.cpp")

m_gtest_program(
  EnvironmentSettingsService
  SOURCES
    "EnvironmentSettingsService_test.cpp"
)

m_gtest_program(
  RenderingSettingsService
  SOURCES
    "RenderingSettingsService_test.cpp"
)

m_gtest_program(
  DiagnosticsPanel
  SOURCES
    "DiagnosticsPanel_test.cpp"
)

m_gtest_program(
  AsyncVortexMigrationSurface
  SOURCES
    "AsyncVortexMigrationSurface_test.cpp"
)

m_gtest_program(FrameCaptureCli SOURCES "FrameCaptureCli_test.cpp")

m_gtest_program(GraphicsToolingCli SOURCES "GraphicsToolingCli_test.cpp")
target_link_libraries(
  "Oxygen.Examples.DemoShell.GraphicsToolingCli.Tests"
  PRIVATE
    oxygen::clap
)
