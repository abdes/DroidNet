# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

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

m_gtest_program(Macros SOURCES "Macros_test.cpp")
m_gtest_program(Config SOURCES "Platform_test.cpp")
m_gtest_program(TimeUtils SOURCES "Time_test.cpp")
m_gtest_program(Types SOURCES "Types_test.cpp")
m_gtest_program(StringUtils SOURCES "StringUtils_test.cpp")
m_gtest_program(Finally SOURCES "Finally_test.cpp")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  m_gtest_program(
    WinUtils
    SOURCES
      "Exceptions_test.cpp"
      "ComError_test.cpp"
  )
endif()

m_gtest_program(
  TypeList
  SOURCES
    "TypeList_test.cpp"
)

m_gtest_program(
  Resource
  SOURCES
    "Resource_test.cpp"
    "ResourceHandle_test.cpp"
    "ResourceTable_test.cpp"
)

m_gtest_program(StaticVector SOURCES "StaticVector_test.cpp")
m_gtest_program(Logging SOURCES "Logging_test.cpp")
m_gtest_program(
  StateMachine
  SOURCES
    "StateMachine_test.cpp"
    "StateMachine_doc_test.cpp"
)
m_gtest_program(NamedType SOURCES "NamedType_test.cpp")
m_gtest_program(ObserverPtr SOURCES "ObserverPtr_test.cpp")

# -- Catch2 Test Programs -------------------------------------------------------

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
