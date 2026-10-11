# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

if(NOT DEFINED OXYGEN_MODULES)
  set(
    OXYGEN_MODULES
    ""
    CACHE STRING
    "Reusable modules to build; empty means the standalone full engine."
  )
endif()

if(NOT PROJECT_IS_TOP_LEVEL AND OXYGEN_MODULES STREQUAL "")
  message(
    FATAL_ERROR
    "Embedding Oxygen requires an explicit OXYGEN_MODULES selection: "
    "Base;Composition;OxCo;Serio;TextWrap;Clap;Config;Console;Core;Platform;Input."
  )
endif()

set(OXYGEN_BUILD_FULL_ENGINE TRUE)
set(OXYGEN_ENABLED_MODULES "")
if(NOT OXYGEN_MODULES STREQUAL "")
  set(OXYGEN_BUILD_FULL_ENGINE FALSE)
  # Dependency order. Every module requires Base; the closure below adds the
  # other modules each one links against.
  set(
    _oxygen_available_modules
    Base
    Composition
    OxCo
    Serio
    TextWrap
    Clap
    Config
    Console
    Core
    Platform
    Input
  )
  foreach(_module IN LISTS OXYGEN_MODULES)
    if(NOT _module IN_LIST _oxygen_available_modules)
      message(
        FATAL_ERROR
        "Unknown reusable Oxygen module '${_module}'. Supported: ${_oxygen_available_modules}"
      )
    endif()
  endforeach()
  set(
    _oxygen_requested_modules
    ${OXYGEN_MODULES}
    Base
  )
  # Direct module dependencies, applied in reverse dependency order so the
  # closure is complete after one pass.
  if("Input" IN_LIST _oxygen_requested_modules)
    list(
      APPEND
      _oxygen_requested_modules
      Console
      Platform
      Core
    )
  endif()
  if("Platform" IN_LIST _oxygen_requested_modules)
    list(
      APPEND
      _oxygen_requested_modules
      Config
      Core
      Composition
      OxCo
    )
  endif()
  if("Core" IN_LIST _oxygen_requested_modules)
    list(
      APPEND
      _oxygen_requested_modules
      Composition
      OxCo
    )
  endif()
  if("Console" IN_LIST _oxygen_requested_modules)
    list(APPEND _oxygen_requested_modules Config)
  endif()
  if("Clap" IN_LIST _oxygen_requested_modules)
    list(APPEND _oxygen_requested_modules TextWrap)
  endif()
  foreach(_module IN LISTS _oxygen_available_modules)
    if(_module IN_LIST _oxygen_requested_modules)
      list(APPEND OXYGEN_ENABLED_MODULES "${_module}")
    endif()
  endforeach()
endif()
