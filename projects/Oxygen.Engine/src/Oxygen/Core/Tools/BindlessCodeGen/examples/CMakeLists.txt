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

if(OXYGEN_BUILD_TESTS)
  add_test(
    NAME BindlessExamplesValidate
    COMMAND
      ${Python3_EXECUTABLE} ${RUN_SCRIPT}
    WORKING_DIRECTORY ${EXAMPLES_DIR}
  )

  set_tests_properties(
    BindlessExamplesValidate
    PROPERTIES
      FAIL_REGULAR_EXPRESSION
        "failed validation"
  )
endif()
