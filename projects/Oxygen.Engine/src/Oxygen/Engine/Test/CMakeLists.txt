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

add_executable("Oxygen.Engine.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Engine.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Engine.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Engine.LinkTest"
  PRIVATE
    oxygen::engine
    oxygen::loader
)
add_test(NAME "Oxygen.Engine.LinkTest" COMMAND "Oxygen.Engine.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(System SOURCES "System_test.cpp")
m_gtest_program(InputHandler SOURCES "InputHandler_test.cpp")
m_gtest_program(EngineModule SOURCES "EngineModule_test.cpp")
m_gtest_program(FrameContext SOURCES "FrameContext_test.cpp")
m_gtest_program(
  ModuleManager
  SOURCES
    "ModuleManager_helpers.h"
    "ModuleManager_test.cpp"
    "ModuleManager_error_test.cpp"
)
