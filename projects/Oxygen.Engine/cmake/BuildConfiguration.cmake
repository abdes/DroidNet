# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Multi-config generators select their configuration at build time. For
# single-config generators, preserve the caller's choice and default only an
# absent or empty build type.
get_property(_oxygen_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(
  NOT
    _oxygen_is_multi_config
  AND
    (
      NOT
        DEFINED
          CMAKE_BUILD_TYPE
      OR
        CMAKE_BUILD_TYPE
          STREQUAL
          ""
    )
)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  set(CMAKE_BUILD_TYPE Debug)
  message(STATUS "No build type specified; defaulting to Debug.")
endif()
