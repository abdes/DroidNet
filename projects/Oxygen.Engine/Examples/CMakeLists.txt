# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

set(OXYGEN_EXAMPLES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# add_subdirectory("AsyncEngine")
add_subdirectory("Platform")
add_subdirectory("Devices")
add_subdirectory("Graphics")

# OxCo Examples
add_subdirectory("OxCo/BatchExecution")
