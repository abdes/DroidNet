# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

find_package(GTest REQUIRED CONFIG)

include(GTestHelpers)

m_gtest_program(SceneInstantiation SOURCES "SceneInstantiation_test.cpp")
target_link_libraries(
  "${META_MODULE_NAME}.SceneInstantiation.Tests"
  PRIVATE
    oxygen::input
)
