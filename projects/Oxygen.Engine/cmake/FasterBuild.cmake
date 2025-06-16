# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

if(__faster_build_description)
  return()
endif()
set(__faster_build_description YES)

# ------------------------------------------------------------------------------
# Reduce build time by using ccache when available
# ------------------------------------------------------------------------------
if(NOT OXYGEN_USE_CCACHE)
  return()
endif()

find_program(CCACHE_TOOL_PATH ccache)
if(CCACHE_TOOL_PATH)
  message(STATUS "Using ccache at (${CCACHE_TOOL_PATH}).")

    include(cmake/CPM.cmake)
    # see https://github.com/TheLartians/Ccache.cmake enables CCACHE support
    # through the USE_CCACHE flag possible values are: YES, NO or equivalent
    set(USE_CCACHE ON)
    cpmaddpackage("gh:TheLartians/Ccache.cmake@1.2.5")
  if(MSVC)
    message(STATUS "Using ccache with MSVC")
    message(STATUS "Setting MSVC Debug Information Flags to `Embedded`")
    # Ccache with MSVC does not support /Zi option, which is added by default,
    # unless we set the CMAKE_MSVC_DEBUG_INFORMATION_FORMAT to Embedded.
    # https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_DEBUG_INFORMATION_FORMAT.html
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_TOOL_PATH} CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_TOOL_PATH} CACHE STRING "" FORCE)
  endif()
else()
  message(STATUS "No ccache tool installed.")
  set(OXYGEN_USE_CCACHE OFF)
endif()
