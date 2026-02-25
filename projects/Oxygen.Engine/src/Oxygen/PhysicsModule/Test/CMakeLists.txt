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

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Sync
  SOURCES
    "EngineTag_test_impl.cpp"
    "PhysicsModule_backend_smoke_test.cpp"
    "PhysicsModule_stress_test.cpp"
    "PhysicsModule_test_fixture.h"
    "Fakes/BackendState.h"
    "Fakes/FakeAggregateApi.h"
    "Fakes/FakeAreaApi.h"
    "Fakes/FakeBodyApi.h"
    "Fakes/FakeCharacterApi.h"
    "Fakes/FakeEventApi.h"
    "Fakes/FakeJointApi.h"
    "Fakes/FakePhysicsSystem.h"
    "Fakes/FakeQueryApi.h"
    "Fakes/FakeShapeApi.h"
    "Fakes/FakeSoftBodyApi.h"
    "Fakes/FakeVehicleApi.h"
    "Fakes/FakeWorldApi.h"
    "PhysicsModule_sync_test.cpp"
)
