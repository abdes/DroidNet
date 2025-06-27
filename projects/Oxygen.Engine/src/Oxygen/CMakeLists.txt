# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Disable MSVC warning C4324 for all targets in subdirectories
if(MSVC)
  add_compile_options(/wd4324)
endif()

add_subdirectory("Base")
add_subdirectory("Composition")
add_subdirectory("Config")
add_subdirectory("Core")
add_subdirectory("Engine")
add_subdirectory("Graphics")
add_subdirectory("ImGui")
add_subdirectory("Input")
add_subdirectory("Loader")
add_subdirectory("OxCo")
add_subdirectory("Platform")
add_subdirectory("Scene")
add_subdirectory("Data")
add_subdirectory("Renderer")

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Testing")
endif()
