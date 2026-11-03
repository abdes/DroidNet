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
target_link_libraries(${link_test_target} PRIVATE oxygen::content)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(ContentOwnership SOURCES "ContentReleaseQueue_test.cpp")
target_sources(
  "${META_MODULE_NAME}.ContentOwnership.Tests"
  PRIVATE
    "../Internal/ContentReleaseQueue.cpp"
    "../Internal/ContentBindingBundle.cpp"
)

m_gtest_program(ContentIdentity SOURCES "ContentIdentityRegistry_test.cpp")
target_sources(
  "${META_MODULE_NAME}.ContentIdentity.Tests"
  PRIVATE
    "../Internal/ContentIdentity.cpp"
    "../Internal/ContentIdentityRegistry.cpp"
)

m_gtest_program(
  AssetLoader
  SOURCES
    "AssetLoader_async_test.cpp"
    "AssetLoader_publication_test.cpp"
    "Fixtures/AssetLoaderSources.h"
    "Fixtures/AssetLoaderSources.cpp"
    "AssetLoader_generation_test.cpp"
    "AssetLoader_physics_test.cpp"
    "AssetLoader_basic_test.cpp"
    "AssetLoader_buffer_test.cpp"
    "AssetLoader_environment_test.cpp"
    "AssetLoader_eviction_test.cpp"
    "AssetLoader_eviction_reentrancy_test.cpp"
    "AssetLoader_autotrim_test.cpp"
    "AssetLoader_lifetime_test.cpp"
    "AssetLoader_loading_test.cpp"
    "AssetLoader_scene_test.cpp"
    "AssetLoader_scene_query_test.cpp"
    "AssetLoader_texture_path_test.cpp"
    "AssetLoader_scripting_test.cpp"
    "AssetLoader_test_fixture.cpp"
    "AssetLoader_test.h"
    "Utils/PakUtils.h"
)
target_link_libraries(
  "${META_MODULE_NAME}.AssetLoader.Tests"
  PRIVATE
    oxygen::cooker::testing
    oxygen::input
)

m_gtest_program(
  LooseCooked
  SOURCES
    "AssetLoader_test_fixture.cpp"
    "AssetLoader_test.h"
    "LooseCookedIndex_test.cpp"
    "LooseCookedSource_test.cpp"
    "VirtualPathResolver_test.cpp"
)
target_link_libraries(
  "${META_MODULE_NAME}.LooseCooked.Tests"
  PRIVATE
    oxygen::cooker::testing
)
target_sources(
  "${META_MODULE_NAME}.LooseCooked.Tests"
  PRIVATE
    "../Internal/IContentSource.cpp"
)

m_gtest_program(
  Loaders
  SOURCES
    "AssetHeaderLoader_test.cpp"
    "BufferLoader_test.cpp"
    "GeometryLoader_test.cpp"
    "InputLoaders_test.cpp"
    "MaterialLoader_test.cpp"
    "SceneLoader_composition_test.cpp"
    "SceneLoader_test.cpp"
    "TextureLoader_test.cpp"
)
target_link_libraries(
  "${META_MODULE_NAME}.Loaders.Tests"
  PRIVATE
    oxygen::cooker::testing
)

m_gtest_program(PayloadBudget SOURCES "PayloadBudget_test.cpp")

m_gtest_program(ContentSources SOURCES "PakFileSource_test.cpp")
target_link_libraries(
  "${META_MODULE_NAME}.ContentSources.Tests"
  PRIVATE
    oxygen::cooker::testing
    oxygen::serio::testing
)
# The source interface's out-of-line members are internal to the library.
target_sources(
  "${META_MODULE_NAME}.ContentSources.Tests"
  PRIVATE
    "../Internal/IContentSource.cpp"
)

m_gtest_program(PakFile SOURCES "PakFile_test.cpp")
target_link_libraries(
  "${META_MODULE_NAME}.PakFile.Tests"
  PRIVATE
    oxygen::cooker::testing
    oxygen::serio::testing
)

find_package(nlohmann_json CONFIG REQUIRED)
m_gtest_program(VirtualPath SOURCES "VirtualPath_test.cpp")
target_link_libraries(
  "${META_MODULE_NAME}.VirtualPath.Tests"
  PRIVATE
    nlohmann_json::nlohmann_json
)
# The case table is the contract shared with the managed implementation; the
# test reads it where it lives, beside VirtualPath.h.
target_compile_definitions(
  "${META_MODULE_NAME}.VirtualPath.Tests"
  PRIVATE
    OXYGEN_CONTENT_VIRTUAL_PATH_CASES="${CMAKE_CURRENT_SOURCE_DIR}/../VirtualPathCases.json"
)
m_gtest_program(PatchResolution SOURCES "PatchResolution_test.cpp")
target_sources(
  "Oxygen.Content.PatchResolution.Tests"
  PRIVATE
    "../Internal/PatchResolutionPolicy.cpp"
    "../Internal/PatchResolutionPolicy.h"
)
target_link_libraries(
  "Oxygen.Content.PatchResolution.Tests"
  PRIVATE
    oxygen::cooker::testing
)

m_gtest_program(
  LoadersPhysics
  SOURCES
    "PhysicsResourceLoader_test.cpp"
    "PhysicsSceneLoader_test.cpp"
    "PhysicsSidecarStructure_test.cpp"
)
target_link_libraries(
  "${META_MODULE_NAME}.LoadersPhysics.Tests"
  PRIVATE
    oxygen::cooker::testing
)

m_gtest_program(
  SourceRegistry
  SOURCES
    "ContentSourceRegistry_test.cpp"
    "ContentSourceRegistry_requirements_test.cpp"
    "Fixtures/FakeContentSource.h"
    "AssetLoader_test_fixture.cpp"
    "AssetLoader_test.h"
)
target_link_libraries(
  "Oxygen.Content.SourceRegistry.Tests"
  PRIVATE
    oxygen::cooker::testing
)
target_sources(
  "Oxygen.Content.SourceRegistry.Tests"
  PRIVATE
    "../Internal/ContentSourceRegistry.cpp"
    "../Internal/ContentSourceRegistry.h"
    "../Internal/IContentSource.cpp"
    "../Internal/PatchResolutionPolicy.cpp"
)
