# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

add_subdirectory("ImportTool")
add_subdirectory("Inspector")
add_subdirectory("PakTool")
add_subdirectory("PakDump")
if(OXYGEN_BUILD_TOOLS OR OXYGEN_BUILD_TESTS)
  add_subdirectory("PakGen")
endif()
