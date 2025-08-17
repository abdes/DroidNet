# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Bindless YAML specifications Examples
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(EXAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(RUN_SCRIPT ${EXAMPLES_DIR}/run_validate_examples.py)

if(BUILD_TESTING)
  add_test(NAME BindlessExamplesValidate
    COMMAND ${Python3_EXECUTABLE} ${RUN_SCRIPT}
    WORKING_DIRECTORY ${EXAMPLES_DIR}
  )

  # Make sure the editable install exists before running the examples test if
  # the top-level build created it.
  if(TARGET bindless_codegen_editable_install)
    set_tests_properties(BindlessExamplesValidate PROPERTIES
      DEPENDS bindless_codegen_editable_install
      FAIL_REGULAR_EXPRESSION "failed validation"
    )
  else()
    set_tests_properties(BindlessExamplesValidate PROPERTIES
      FAIL_REGULAR_EXPRESSION "failed validation"
    )
  endif()
endif()
