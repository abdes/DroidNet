# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Shared qualification infrastructure, compiled once for tests and benchmarks.
add_library(
  oxygen-vortex-timing-test-support
  STATIC
  "CpuTimingCapture.cpp"
  "CpuTimingCapture.h"
)
set_target_properties(
  oxygen-vortex-timing-test-support
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(
  oxygen-vortex-timing-test-support
  PUBLIC
    oxygen::core
    oxygen::profiling
    nlohmann_json::nlohmann_json
)
