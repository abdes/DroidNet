# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

include_guard(DIRECTORY)

# ------------------------------------------------------------------------------
# Reduce build time by using ccache when available
# ------------------------------------------------------------------------------
set(OXYGEN_CCACHE_STATUS "automatic selection disabled")
if(OXYGEN_USE_CCACHE)
  if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles|WMake")
    set(
      OXYGEN_CCACHE_STATUS
      "automatic selection unavailable for ${CMAKE_GENERATOR}"
    )
    message(
      WARNING
      "OXYGEN_USE_CCACHE: ${CMAKE_GENERATOR} does not support native compiler launchers. "
      "Continuing without automatic ccache integration."
    )
  else()
    set(_oxygen_cache_languages)
    foreach(_language IN ITEMS C CXX)
      # An explicitly empty launcher is also a caller choice. Normal variables
      # avoid leaving an automatic launcher behind when the option is disabled.
      if(NOT DEFINED CMAKE_${_language}_COMPILER_LAUNCHER)
        list(APPEND _oxygen_cache_languages ${_language})
      endif()
    endforeach()
    if(_oxygen_cache_languages)
      find_program(CCACHE_TOOL_PATH NAMES ccache)
      if(
        CCACHE_TOOL_PATH
        AND
          EXISTS
            "${CCACHE_TOOL_PATH}"
        AND
          NOT
            IS_DIRECTORY
              "${CCACHE_TOOL_PATH}"
      )
        foreach(_language IN LISTS _oxygen_cache_languages)
          set(CMAKE_${_language}_COMPILER_LAUNCHER "${CCACHE_TOOL_PATH}")
        endforeach()
        list(JOIN _oxygen_cache_languages "/" _languages)
        set(
          OXYGEN_CCACHE_STATUS
          "automatic ${_languages} launcher: ${CCACHE_TOOL_PATH}"
        )
      else()
        set(OXYGEN_CCACHE_STATUS "unavailable (ccache not found)")
        message(
          WARNING
          "OXYGEN_USE_CCACHE: ccache was not found. Continuing with normal compilation."
        )
      endif()
    else()
      set(OXYGEN_CCACHE_STATUS "caller-selected launchers preserved")
    endif()
  endif()
endif()
