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

set(link_test_target "Oxygen.TextWrap.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${link_test_target} PRIVATE oxygen::textwrap)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(DocExample SOURCES "TextWrap_doc_test.cpp")
m_gtest_program(Tokenizer SOURCES "Tokenizer_test.cpp")
m_gtest_program(
  Fill
  SOURCES
    "TextWrap_basic_test.cpp"
    "TextWrap_edge_test.cpp"
    "TextWrap_indent_test.cpp"
    "TextWrap_basic_test.cpp"
    "TextWrap_ansi_strip_test.cpp"
)

# -- Fuzzing Test --------------------------------------------------------------

set(fuzz_test_target "Oxygen.TextWrap.FuzzingTest")
add_executable(${fuzz_test_target} "TextWrap_random_test.cpp")
set_target_properties(
  ${fuzz_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${fuzz_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(
  ${fuzz_test_target}
  PRIVATE
    oxygen::base
    oxygen::textwrap
)
