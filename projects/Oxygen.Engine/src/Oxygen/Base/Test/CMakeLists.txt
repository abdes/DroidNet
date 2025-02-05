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

add_executable("Oxygen.Base.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Base.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Base.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries("Oxygen.Base.LinkTest" PRIVATE oxygen::base)
add_test(NAME "Oxygen.Base Link Test" COMMAND "Oxygen.Base.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

# -- GTest custom main ---------------------------------------------------------

add_library(gtest_main STATIC "gtest_main.cpp")
add_library(${META_MODULE_TARGET_ALIAS}::gtest ALIAS gtest_main)
set_target_properties(
  gtest_main
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(gtest_main PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(
  gtest_main
  PUBLIC
    GTest::gtest
    GTest::gmock
  PRIVATE
    oxygen::base
)

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
  "Oxygen.Base.TypeSystem.Tests"
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wl,--export-dynamic>
)

m_gtest_program(Macros SOURCES "Macros_test.cpp")
m_gtest_program(Config SOURCES "Platform_test.cpp")
m_gtest_program(TimeUtils SOURCES "Time_test.cpp")
m_gtest_program(Types SOURCES "Types_test.cpp")
m_gtest_program(StringUtils SOURCES "StringUtils_test.cpp")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  m_gtest_program(
    WinUtils
    SOURCES
      "Exceptions_test.cpp"
      "ComError_test.cpp"
  )
endif()

m_gtest_program(
  Serio
  SOURCES
    "FileStream_test.cpp"
    "MemoryStream_test.cpp"
    "Reader_test.cpp"
    "Writer_test.cpp"
)

m_gtest_program(
  Composition
  SOURCES
    "ts_init.cpp"
    "Composition_test.cpp"
    "ObjectMetaData_test.cpp"
)

m_gtest_program(
        Resource
        SOURCES
        "Resource_test.cpp"
        "ResourceHandle_test.cpp"
        "ResourceTable_test.cpp"
)

include(Catch2Helpers)

# -- Catch2 custom main --------------------------------------------------------

add_library(catch2_main STATIC "catch2_main.cpp")
add_library(${META_MODULE_TARGET_ALIAS}::catch2 ALIAS catch2_main)
set_target_properties(
  catch2_main
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(catch2_main PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(
  catch2_main
  PUBLIC
    Catch2::Catch2
    oxygen::base
)

m_catch2_program(Unreachable SOURCES "Unreachable_test.cpp")
m_catch2_program(NoInline SOURCES "NoInline_test.cpp")
m_catch2_program(ReturnAddress SOURCES "ReturnAddress_test.cpp")
