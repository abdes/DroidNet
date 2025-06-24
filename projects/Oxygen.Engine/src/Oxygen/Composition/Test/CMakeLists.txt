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

add_executable("Oxygen.Composition.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Composition.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Composition.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Composition.LinkTest"
  PRIVATE
    oxygen::base
    oxygen::composition
)
add_test(
  NAME "Oxygen.Composition Link Test"
  COMMAND
    "Oxygen.Composition.LinkTest"
)

# -- Global Initialization Tests -----------------------------------------------

add_subdirectory("GlobalInit")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(TypeSystem SOURCES "TypeSystem_test.cpp")

m_gtest_program(ObjectMetaData SOURCES "ObjectMetaData_test.cpp")

m_gtest_program(ComponentPool SOURCES "ComponentPool_test.cpp")
m_gtest_program(ComponentPoolRegistry SOURCES "ComponentPoolRegistry_test.cpp")

m_gtest_program(CompositionBasic SOURCES "Composition_Basic_test.cpp")
m_gtest_program(CompositionCloning SOURCES "Composition_Cloning_test.cpp")
m_gtest_program(CompositionDeps SOURCES "Composition_Dependency_test.cpp")
m_gtest_program(CompositionErrors SOURCES "Composition_ErrorHandling_test.cpp")
m_gtest_program(CompositionHybrid SOURCES "Composition_Hybrid_test.cpp")
m_gtest_program(CompositionPooled SOURCES "Composition_Pooled_test.cpp")
m_gtest_program(CompositionPrint SOURCES "Composition_PrintAndDebug_test.cpp")
m_gtest_program(CompositionMemory SOURCES "memory_invalidation_test.cpp")
m_gtest_program(CompositionThreads SOURCES "Composition_Threads_test.cpp")

add_custom_target(
  "Oxygen.Composition.Tests"
  DEPENDS
    "Oxygen.Composition.ObjectMetaData.Tests"
    "Oxygen.Composition.ComponentPool.Tests"
    "Oxygen.Composition.ComponentPoolRegistry.Tests"
    "Oxygen.Composition.CompositionBasic.Tests"
    "Oxygen.Composition.CompositionCloning.Tests"
    "Oxygen.Composition.CompositionDeps.Tests"
    "Oxygen.Composition.CompositionErrors.Tests"
    "Oxygen.Composition.CompositionHybrid.Tests"
    "Oxygen.Composition.CompositionPooled.Tests"
    "Oxygen.Composition.CompositionPrint.Tests"
    "Oxygen.Composition.CompositionMemory.Tests"
    "Oxygen.Composition.CompositionThreads.Tests"
)
