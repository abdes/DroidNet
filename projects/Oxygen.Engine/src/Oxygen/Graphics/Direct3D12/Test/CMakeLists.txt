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

add_executable("Oxygen.Graphics.Direct3D12.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Graphics.Common.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Graphics.Direct3D12.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
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
m_gtest_program(DescriptorSegment SOURCES "DescriptorSegment_test.cpp")
m_gtest_program(PipelineStateCache SOURCES "PipelineStateCache_test.cpp")
m_gtest_program(
  TextureReadbackLayout
  SOURCES
    "TextureReadbackLayout_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  TextureReadbackIntegration
  SOURCES
    "TextureReadbackIntegration_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  BufferReadbackIntegration
  SOURCES
    "BufferReadbackIntegration_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
m_gtest_program(
  SubmitOrderedQueueActions
  SOURCES
    "SubmitOrderedQueueActions_test.cpp"
    ${D3D12_TEST_FIXTURES}
)
