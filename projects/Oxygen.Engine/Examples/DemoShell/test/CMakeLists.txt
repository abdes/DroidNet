# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

include(GTestHelpers)

m_gtest_program(SceneLoaderService SOURCES "SceneLoaderService_test.cpp")

m_gtest_program(SettingsService SOURCES "SettingsService_test.cpp")
m_gtest_program(FileBrowser SOURCES "FileBrowser_test.cpp")
# The content panel's tests write their cooked sources with the cooked content
# builders.
m_gtest_program(
  ContentVm
  SOURCES
    "ContentVm_test.cpp"
    "ContentFixtures.cpp"
    "ContentFixtures.h"
)
m_gtest_program(
  ContentLoaderPanel
  SOURCES
    "ContentLoaderPanel_test.cpp"
    "ContentFixtures.cpp"
    "ContentFixtures.h"
)
foreach(_content_test IN ITEMS ContentVm ContentLoaderPanel)
  target_link_libraries(
    "${META_MODULE_NAME}.${_content_test}.Tests"
    PRIVATE
      oxygen::cooker::testing
  )
endforeach()

m_gtest_program(PostProcessConsole SOURCES "PostProcessConsole_test.cpp")

m_gtest_program(DefaultSceneLighting SOURCES "DefaultSceneLighting_test.cpp")
m_gtest_program(PreviewSunController SOURCES "PreviewSunController_test.cpp")
m_gtest_program(
  EnvironmentSceneSnapshot
  SOURCES
    "EnvironmentSceneSnapshot_test.cpp"
)
m_gtest_program(SkyboxService SOURCES "SkyboxService_test.cpp")

m_gtest_program(OrbitCameraController SOURCES "OrbitCameraController_test.cpp")
m_gtest_program(DroneCameraController SOURCES "DroneCameraController_test.cpp")
m_gtest_program(CameraSettingsService SOURCES "CameraSettingsService_test.cpp")

m_gtest_program(
  EnvironmentSettingsService
  SOURCES
    "EnvironmentSettingsService_test.cpp"
)

m_gtest_program(EnvironmentVm SOURCES "EnvironmentVm_test.cpp")
m_gtest_program(
  PostProcessSettingsService
  SOURCES
    "PostProcessSettingsService_test.cpp"
)

m_gtest_program(
  RenderingSettingsService
  SOURCES
    "RenderingSettingsService_test.cpp"
)

m_gtest_program(DiagnosticsPanel SOURCES "DiagnosticsPanel_test.cpp")

# These programs share the demo settings' default file; CTest runs them one at a
# time so none reads another's writes.
foreach(
  _settings_test
  IN
  ITEMS
    ContentLoaderPanel
    DiagnosticsPanel
    EnvironmentSettingsService
    EnvironmentVm
    OrbitCameraController
    PostProcessConsole
    PostProcessSettingsService
    RenderingSettingsService
)
  set_tests_properties(
    "${META_MODULE_NAME}.${_settings_test}.Tests"
    PROPERTIES
      RESOURCE_LOCK
        "DemoShellDemoSettings"
  )
endforeach()
