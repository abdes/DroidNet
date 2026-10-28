# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

add_executable("Oxygen.Graphics.Direct3D12.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Graphics.Direct3D12.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(
  "Oxygen.Graphics.Direct3D12.LinkTest"
  PRIVATE
    oxygen::graphics-direct3d12
)
add_test(
  NAME "Oxygen.Graphics.Direct3D12 Link Test"
  COMMAND
    "Oxygen.Graphics.Direct3D12.LinkTest"
)

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

set(
  D3D12_TEST_FIXTURES
  "Fixtures/OffscreenTestFixture.h"
  "Fixtures/ReadbackTestFixture.h"
)

m_gtest_program(
  D3D12HeapAllocationStrategy
  SOURCES
    "HeapAllocationStrategy_domain_test.cpp"
    "HeapAllocationStrategy_test.cpp"
)
m_gtest_program(AdapterSelection SOURCES "AdapterSelection_test.cpp")
m_gtest_program(
  DeviceManagerSelection
  GPU
  SOURCES
    "DeviceManagerSelection_test.cpp"
)
m_gtest_program(DescriptorSegment SOURCES "DescriptorSegment_test.cpp")
m_gtest_program(
  PipelineStateCache
  SOURCES
    "PipelineStateCache_test.cpp"
    "PipelineBindingState_test.cpp"
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests"
  SOURCES
    "ShaderBakeCatalog_test.cpp"
    "../Tools/ShaderBake/Catalog.cpp"
    "../Tools/ShaderBake/Catalog.h"
  DEPS
    oxygen::graphics-common
    oxygen::testing
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeManifest.Tests"
  SOURCES
    "ShaderBakeManifest_test.cpp"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/Catalog.cpp"
    "../Tools/ShaderBake/Catalog.h"
    "../Tools/ShaderBake/Manifest.cpp"
    "../Tools/ShaderBake/Manifest.h"
  DEPS
    oxygen::graphics-common
    oxygen::testing
    nlohmann_json::nlohmann_json
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeModuleArtifact.Tests"
  SOURCES
    "ShaderBakeModuleArtifact_test.cpp"
    "../Tools/ShaderBake/ActionKey.cpp"
    "../Tools/ShaderBake/ActionKey.h"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/ModuleArtifact.cpp"
    "../Tools/ShaderBake/ModuleArtifact.h"
  DEPS
    # ActionKey.cpp reads file versions through the Windows version library.
    $<$<PLATFORM_ID:Windows>:version>
    oxygen::graphics-common
    oxygen::serio
    oxygen::testing
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeBuildState.Tests"
  SOURCES
    "ShaderBakeBuildState_test.cpp"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/BuildState.cpp"
    "../Tools/ShaderBake/BuildState.h"
    "../Tools/ShaderBake/ModuleArtifact.cpp"
    "../Tools/ShaderBake/ModuleArtifact.h"
  DEPS
    oxygen::graphics-common
    oxygen::serio
    oxygen::testing
    nlohmann_json::nlohmann_json
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeDependencies.Tests"
  SOURCES
    "ShaderBakeDependencies_test.cpp"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/FileFingerprint.cpp"
    "../Tools/ShaderBake/FileFingerprint.h"
    "../Tools/ShaderBake/TrackingIncludeHandler.cpp"
    "../Tools/ShaderBake/TrackingIncludeHandler.h"
  DEPS
    oxygen::graphics-common
    oxygen::testing
    dxc::dxcompiler
)
oxygen_stage_dxc_runtime(
  Oxygen.Graphics.Direct3D12.ShaderBakeDependencies.Tests
)

gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeDirtyAnalysis.Tests"
  SOURCES
    "ShaderBakeDirtyAnalysis_test.cpp"
    "../Tools/ShaderBake/ActionKey.cpp"
    "../Tools/ShaderBake/ActionKey.h"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/BuildState.cpp"
    "../Tools/ShaderBake/BuildState.h"
    "../Tools/ShaderBake/Catalog.h"
    "../Tools/ShaderBake/DirtyAnalysis.cpp"
    "../Tools/ShaderBake/DirtyAnalysis.h"
    "../Tools/ShaderBake/FileFingerprint.cpp"
    "../Tools/ShaderBake/FileFingerprint.h"
    "../Tools/ShaderBake/Manifest.cpp"
    "../Tools/ShaderBake/Manifest.h"
    "../Tools/ShaderBake/ModuleArtifact.cpp"
    "../Tools/ShaderBake/ModuleArtifact.h"
  DEPS
    # ActionKey.cpp reads file versions through the Windows version library.
    $<$<PLATFORM_ID:Windows>:version>
    oxygen::graphics-common
    oxygen::serio
    oxygen::testing
    nlohmann_json::nlohmann_json
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeExecution.Tests"
  SOURCES
    "ShaderBakeExecution_test.cpp"
    "../Tools/ShaderBake/ActionKey.cpp"
    "../Tools/ShaderBake/ActionKey.h"
    "../Tools/ShaderBake/Bake.h"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/BuildState.cpp"
    "../Tools/ShaderBake/BuildState.h"
    "../Tools/ShaderBake/Catalog.h"
    "../Tools/ShaderBake/DirtyAnalysis.cpp"
    "../Tools/ShaderBake/DirtyAnalysis.h"
    "../Tools/ShaderBake/Execution.cpp"
    "../Tools/ShaderBake/Execution.h"
    "../Tools/ShaderBake/FileFingerprint.cpp"
    "../Tools/ShaderBake/FileFingerprint.h"
    "../Tools/ShaderBake/FinalArchivePack.cpp"
    "../Tools/ShaderBake/FinalArchivePack.h"
    "../Tools/ShaderBake/Manifest.cpp"
    "../Tools/ShaderBake/Manifest.h"
    "../Tools/ShaderBake/ModuleArtifact.cpp"
    "../Tools/ShaderBake/ModuleArtifact.h"
    "../Tools/ShaderBake/RequestCompilation.h"
  DEPS
    # ActionKey.cpp reads file versions through the Windows version library.
    $<$<PLATFORM_ID:Windows>:version>
    oxygen::graphics-common
    oxygen::serio
    oxygen::testing
    nlohmann_json::nlohmann_json
)
gtest_program(
  "Oxygen.Graphics.Direct3D12.ShaderBakeRequestCompilation.Tests"
  SOURCES
    "ShaderBakeRequestCompilation_test.cpp"
    "../Tools/ShaderBake/ActionKey.cpp"
    "../Tools/ShaderBake/ActionKey.h"
    "../Tools/ShaderBake/BuildPaths.cpp"
    "../Tools/ShaderBake/BuildPaths.h"
    "../Tools/ShaderBake/Catalog.h"
    "../Tools/ShaderBake/DxcShaderCompiler.cpp"
    "../Tools/ShaderBake/DxcShaderCompiler.h"
    "../Tools/ShaderBake/FileFingerprint.cpp"
    "../Tools/ShaderBake/FileFingerprint.h"
    "../Tools/ShaderBake/ModuleArtifact.cpp"
    "../Tools/ShaderBake/ModuleArtifact.h"
    "../Tools/ShaderBake/Reflect.cpp"
    "../Tools/ShaderBake/Reflect.h"
    "../Tools/ShaderBake/RequestCompilation.cpp"
    "../Tools/ShaderBake/RequestCompilation.h"
    "../Tools/ShaderBake/TrackingIncludeHandler.cpp"
    "../Tools/ShaderBake/TrackingIncludeHandler.h"
  DEPS
    # ActionKey.cpp reads file versions through the Windows version library.
    $<$<PLATFORM_ID:Windows>:version>
    oxygen::graphics-common
    oxygen::serio
    oxygen::testing
    nlohmann_json::nlohmann_json
    dxc::dxcompiler
)
target_compile_definitions(
  Oxygen.Graphics.Direct3D12.ShaderBakeRequestCompilation.Tests
  PRIVATE
    "OXYGEN_ENGINE_SHADER_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}/../Shaders\""
    "OXYGEN_ENGINE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}/../../..\""
)
oxygen_stage_dxc_runtime(
  Oxygen.Graphics.Direct3D12.ShaderBakeRequestCompilation.Tests
)
m_gtest_program(
  TextureReadbackLayout
  SOURCES
    "TextureReadbackLayout_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  TextureReadbackIntegration
  GPU
  SOURCES
    "TextureReadbackIntegration_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  ReadbackSurfaceMapping
  GPU
  SOURCES
    "ReadbackSurfaceMapping_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  TextureReadbackObject
  GPU
  SOURCES
    "TextureReadbackObject_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  BufferReadbackIntegration
  GPU
  SOURCES
    "BufferReadbackIntegration_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  BlockingReadback
  GPU
  SOURCES
    "BlockingReadback_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  SubmitOrderedQueueActions
  GPU
  SOURCES
    "SubmitOrderedQueueActions_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  TimestampQueryBackend
  GPU
  SOURCES
    "TimestampQueryBackend_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  NativeLifetime
  GPU
  SOURCES
    "NativeLifetime_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
