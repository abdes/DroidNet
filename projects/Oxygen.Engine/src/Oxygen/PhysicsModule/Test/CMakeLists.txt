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
    "PhysicsModule_test_fixture.h"
    "PhysicsModule_sync_test.cpp"
)
target_compile_definitions(
  Oxygen.PhysicsModule.Sync.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)
