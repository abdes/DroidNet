# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Generate version-header
configure_file("version.h.in" ${CMAKE_CURRENT_SOURCE_DIR}/version-info.h)

add_subdirectory("Base")
add_subdirectory("Composition")
add_subdirectory("Config")
add_subdirectory("Core")
add_subdirectory("Graphics")
add_subdirectory("ImGui")
add_subdirectory("Input")
add_subdirectory("Loader")
add_subdirectory("OxCo")
add_subdirectory("Platform")

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Testing")
endif()
