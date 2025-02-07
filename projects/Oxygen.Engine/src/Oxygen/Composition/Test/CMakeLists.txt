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
target_link_libraries("Oxygen.Composition.LinkTest" PRIVATE oxygen::base oxygen::composition)
add_test(NAME "Oxygen.Composition Link Test" COMMAND "Oxygen.Composition.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  TypeSystem
  SOURCES
    "ts_init.cpp"
    "TypeSystem_test.cpp"
)

# Tell clang and gcc to export symbols even from the executable.
# Needed so that the type system can find the `InitializeTypeRegistry`
# function defined in the executable.
target_link_options(
  "Oxygen.Composition.TypeSystem.Tests"
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wl,--export-dynamic>
)

m_gtest_program(
  Composition
  SOURCES
    "ts_init.cpp"
    "Composition_test.cpp"
    "ObjectMetaData_test.cpp"
)
