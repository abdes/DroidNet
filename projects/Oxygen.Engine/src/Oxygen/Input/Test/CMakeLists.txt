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

add_executable("Oxygen.Input.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Input.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries("Oxygen.Input.LinkTest" PRIVATE oxygen::input)
add_test(NAME "Oxygen.Input Link Test" COMMAND "Oxygen.Input.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  InputSystem
  SOURCES
    "Action_test.cpp"
    "ActionTriggerChain_test.cpp"
    "ActionTriggerCombo_test.cpp"
    "ActionTriggerDown_test.cpp"
    "ActionTriggerHold_test.cpp"
    "ActionTriggerHoldAndRelease_test.cpp"
    "ActionTriggerPressed_test.cpp"
    "ActionTriggerPulse_test.cpp"
    "ActionTriggerReleased_test.cpp"
    "ActionTriggerTap_test.cpp"
    "ActionValue_test.cpp"
    "InputActionMapping_test.cpp"
    "InputMappingContext_test.cpp"
    "InputSnapshot_test.cpp"
    "InputSystem_basic_test.cpp"
    "InputSystem_integration_test.cpp"
    "ActionContribution_test.cpp"
    "ActionTestAccess.h"
    "InputSystemTest.cpp"
    "InputSystemTest.h"
)

# Functional suite: gameplay scenarios driven through the real InputSystem.
m_gtest_program(
  Scenarios
  SOURCES
    "Scenarios/Bindings_scenario_test.cpp"
    "Scenarios/Consumption_scenario_test.cpp"
    "Scenarios/Contexts_scenario_test.cpp"
    "Scenarios/Lifecycle_scenario_test.cpp"
    "Scenarios/MouseAxes_scenario_test.cpp"
    "Scenarios/ScenarioTest.cpp"
    "Scenarios/ScenarioTest.h"
    "Scenarios/Timing_scenario_test.cpp"
    "InputSystemTest.cpp"
    "InputSystemTest.h"
)
