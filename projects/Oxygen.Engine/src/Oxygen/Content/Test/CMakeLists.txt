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

set(link_test_target "Oxygen.Content.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${link_test_target} PRIVATE oxygen::content)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- Assets Dump Tool ----------------------------------------------------------

set(dump_assets_target "Oxygen.Content.DumpPakFile")
add_executable(${dump_assets_target} "PakFileDumper.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${dump_assets_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${dump_assets_target} PRIVATE oxygen::content)

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Loaders
  SOURCES
    "Mocks/MockStream.h"
    "Utils/PakUtils.h"
    "AssetHeaderLoader_test.cpp"
    "TextureLoader_test.cpp"
    "MaterialLoader_test.cpp"
  #"GeometryLoader_basic_test.cpp"
  #"GeometryLoader_error_test.cpp"
)
