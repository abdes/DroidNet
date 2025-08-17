# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Tools CMake - aggregate tool subdirectories

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/BindlessCodeGen/CMakeLists.txt")
  add_subdirectory("BindlessCodeGen")
endif()
