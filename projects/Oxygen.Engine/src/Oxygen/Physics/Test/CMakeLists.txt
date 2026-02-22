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

set(link_test_target "Oxygen.Physics.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${link_test_target} PRIVATE oxygen::physics)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(Basic SOURCES "Physics_test.cpp")

if(OXYGEN_PHYSICS_BACKEND STREQUAL "jolt")
  m_gtest_program(
    Jolt
    SOURCES
      "Jolt/Jolt_test_fixture.h"
      "Jolt/Body/Jolt_body_basic_test.cpp"
      "Jolt/Body/Jolt_body_contract_test.cpp"
      "Jolt/Body/Jolt_body_domain_test.cpp"
      "Jolt/Aggregate/Jolt_aggregate_basic_test.cpp"
      "Jolt/Aggregate/Jolt_aggregate_contract_test.cpp"
      "Jolt/Aggregate/Jolt_aggregate_domain_test.cpp"
      "Jolt/Articulation/Jolt_articulation_basic_test.cpp"
      "Jolt/Articulation/Jolt_articulation_contract_test.cpp"
      "Jolt/Articulation/Jolt_articulation_domain_test.cpp"
      "Jolt/Area/Jolt_area_basic_test.cpp"
      "Jolt/Area/Jolt_area_contract_test.cpp"
      "Jolt/Area/Jolt_area_domain_test.cpp"
      "Jolt/Character/Jolt_character_basic_test.cpp"
      "Jolt/Character/Jolt_character_contract_test.cpp"
      "Jolt/Character/Jolt_character_domain_test.cpp"
      "Jolt/Event/Jolt_event_basic_test.cpp"
      "Jolt/Event/Jolt_event_contract_test.cpp"
      "Jolt/Event/Jolt_event_domain_test.cpp"
      "Jolt/Joint/Jolt_joint_basic_test.cpp"
      "Jolt/Joint/Jolt_joint_contract_test.cpp"
      "Jolt/Joint/Jolt_joint_domain_test.cpp"
      "Jolt/Query/Jolt_query_basic_test.cpp"
      "Jolt/Query/Jolt_query_contract_test.cpp"
      "Jolt/Query/Jolt_query_domain_test.cpp"
      "Jolt/Shape/Jolt_shape_basic_test.cpp"
      "Jolt/Shape/Jolt_shape_contract_test.cpp"
      "Jolt/Shape/Jolt_shape_domain_test.cpp"
      "Jolt/Vehicle/Jolt_vehicle_basic_test.cpp"
      "Jolt/Vehicle/Jolt_vehicle_contract_test.cpp"
      "Jolt/Vehicle/Jolt_vehicle_domain_test.cpp"
      "Jolt/World/Jolt_world_basic_test.cpp"
      "Jolt/World/Jolt_world_contract_test.cpp"
      "Jolt/World/Jolt_world_domain_test.cpp"
  )
endif()
