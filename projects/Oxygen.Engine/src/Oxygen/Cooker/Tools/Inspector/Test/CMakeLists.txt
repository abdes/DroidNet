# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json_schema_validator REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

oxygen_cooker_test(
  Inspector
  FUNCTIONAL
  DEPS
    oxygen-cooker-inspector-core
    oxygen::data
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "AssetKeyMap_functional_test.cpp"
    "GeometryMetadata_functional_test.cpp"
    "SceneMetadata_functional_test.cpp"
)
