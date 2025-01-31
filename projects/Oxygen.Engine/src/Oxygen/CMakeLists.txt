# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Generate version-header
configure_file("version.h.in" ${CMAKE_CURRENT_SOURCE_DIR}/version-info.h)

add_subdirectory("Base")
add_subdirectory("OxCo")
add_subdirectory("Platform")
add_subdirectory("Core")
add_subdirectory("Input")
add_subdirectory("Graphics")
