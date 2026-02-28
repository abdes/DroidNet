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

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  AssetLoader
  SOURCES
    "AssetLoader_async_test.cpp"
    "AssetLoader_basic_test.cpp"
    "AssetLoader_buffer_test.cpp"
    "AssetLoader_cancellation_test.cpp"
    "AssetLoader_deps_test.cpp"
    "AssetLoader_eviction_test.cpp"
    "AssetLoader_eviction_reentrancy_test.cpp"
    "AssetLoader_autotrim_test.cpp"
    "AssetLoader_lifetime_test.cpp"
    "AssetLoader_loading_test.cpp"
    "AssetLoader_scene_test.cpp"
    "AssetLoader_scripting_test.cpp"
    "AssetLoader_test_fixture.cpp"
    "AssetLoader_test_helpers.cpp"
    "AssetLoader_test.h"
    "Fixtures/LooseCookedTestLayout.h"
    "Utils/PakUtils.h"
)

m_gtest_program(
  LooseCooked
  SOURCES
    "AssetLoader_test_fixture.cpp"
    "AssetLoader_test.h"
    "LooseCookedIndex_test.cpp"
    "LooseCookedSource_test.cpp"
    "VirtualPathResolver_test.cpp"
    "Fixtures/LooseCookedTestLayout.h"
    "Fixtures/LooseCookedTestWriter.h"
)

m_gtest_program(
  Loaders
  SOURCES
    "AssetHeaderLoader_test.cpp"
    "BufferLoader_test.cpp"
    "GeometryLoader_test.cpp"
    "InputLoaders_test.cpp"
    "MaterialLoader_test.cpp"
    "Mocks/MockStream.h"
    "SceneLoader_test.cpp"
    "TextureLoader_test.cpp"
)

m_gtest_program(PakFile SOURCES "PakFile_test.cpp")
m_gtest_program(PatchResolution SOURCES "PatchResolution_test.cpp")

m_gtest_program(
  LoadersPhysics
  SOURCES
    "PhysicsResourceLoader_test.cpp"
    "PhysicsSceneLoader_test.cpp"
)

m_gtest_program(LoadersScripting SOURCES "SceneScriptingLoader_test.cpp")

# Ensure the Loaders tests run only after PakGen editable install so the
# pakgen CLI is available for any dynamic pack builds performed by tests.
add_dependencies("${META_MODULE_NAME}.Loaders.Tests" pakgen_editable_install)
add_dependencies(
  "${META_MODULE_NAME}.LoadersPhysics.Tests"
  pakgen_editable_install
)
