# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# The command line scaffolding shared by the content tools. Internal: it is
# linked into each tool and not installed.
# ------------------------------------------------------------------------------

add_library(
  oxygen-cooker-toolkit
  STATIC
  "ToolApp.cpp"
  "ToolApp.h"
  "ToolCommand.h"
)
set_target_properties(
  oxygen-cooker-toolkit
  PROPERTIES
    FOLDER
      "Tools"
)
target_link_libraries(
  oxygen-cooker-toolkit
  PUBLIC
    oxygen::base
    oxygen::cooker
    oxygen::clap
)

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
  add_subdirectory("test")
endif()
