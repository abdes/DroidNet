# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

option(BUILD_SHARED_LIBS "Build shared instead of static libraries." OFF)
option(
  OXYGEN_BUILD_UI_TESTS
  "Instrument demo applications with ImGui Test Engine."
  OFF
)
option(OXYGEN_BUILD_TESTS "Build Oxygen tests." ${PROJECT_IS_TOP_LEVEL})
# Match CTest's global switch without changing an embedding parent's setting.
option(BUILD_TESTING "Build the testing tree." ON)
if(NOT BUILD_TESTING)
  # A directory-local effective value preserves the user's cached Oxygen choice
  # when global testing is enabled again. Conan validates this narrowing below.
  set(OXYGEN_BUILD_TESTS OFF)
endif()
option(
  OXYGEN_BUILD_EXAMPLES
  "Build development examples in addition to the mandatory RenderScene showcase."
  ${PROJECT_IS_TOP_LEVEL}
)
option(OXYGEN_BUILD_DOCS "Enable Oxygen documentation." ${PROJECT_IS_TOP_LEVEL})
option(
  OXYGEN_BUILD_TOOLS
  "Build optional development tools; native cooker tools and ShaderBake are mandatory."
  ${PROJECT_IS_TOP_LEVEL}
)
option(
  OXYGEN_BUILD_BENCHMARKS
  "Build Oxygen benchmarks."
  ${PROJECT_IS_TOP_LEVEL}
)
option(OXYGEN_WITH_ASAN "Instrument code with address sanitizer." OFF)
option(OXYGEN_WITH_TRACY "Enable Tracy profiler integration." OFF)
option(OXYGEN_WITH_DOXYGEN "Create Doxygen API documentation targets." OFF)
option(
  OXYGEN_USE_CCACHE
  "Select installed ccache automatically when no compiler launcher is specified."
  OFF
)
set(
  OXYGEN_AWAITER_STATE_CHECKER
  AUTO
  CACHE STRING
  "OxCo awaiter checking: AUTO enables Debug; ON or OFF overrides all configurations."
)
set_property(
  CACHE
    OXYGEN_AWAITER_STATE_CHECKER
  PROPERTY
    STRINGS
      AUTO
      ON
      OFF
)
if(NOT OXYGEN_AWAITER_STATE_CHECKER MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "OXYGEN_AWAITER_STATE_CHECKER must be AUTO, ON or OFF.")
endif()
if(
  DEFINED
    OXYGEN_CONAN_AWAITER_STATE_CHECKER
  AND
    NOT
      OXYGEN_AWAITER_STATE_CHECKER
        STREQUAL
        OXYGEN_CONAN_AWAITER_STATE_CHECKER
)
  message(
    FATAL_ERROR
    "Awaiter checking conflicts with Conan. Regenerate dependencies with the requested awaitable_state_checker option."
  )
endif()

if(NOT PROJECT_IS_TOP_LEVEL AND OXYGEN_USE_CCACHE)
  message(
    FATAL_ERROR
    "OXYGEN_USE_CCACHE configures standalone builds. For embedded modules, "
    "set CMAKE_C_COMPILER_LAUNCHER and CMAKE_CXX_COMPILER_LAUNCHER in the parent project."
  )
endif()

# Conan emits defaults in presets, and immutable graph expectations in its
# toolchain. Never replace a caller's normal variable with a stale cache value.
set(
  _oxygen_narrowable_options
  OXYGEN_BUILD_TESTS
  OXYGEN_BUILD_EXAMPLES
  OXYGEN_BUILD_DOCS
  OXYGEN_BUILD_TOOLS
  OXYGEN_BUILD_BENCHMARKS
)
set(
  _oxygen_graph_options
  ${_oxygen_narrowable_options}
  BUILD_SHARED_LIBS
  OXYGEN_WITH_ASAN
  OXYGEN_WITH_TRACY
  OXYGEN_BUILD_UI_TESTS
)
foreach(_option IN LISTS _oxygen_graph_options)
  set(_expected "OXYGEN_CONAN_EXPECT_${_option}")
  if(NOT DEFINED ${_expected})
    continue()
  endif()
  if((${_option} AND NOT ${_expected}) OR (NOT ${_option} AND ${_expected}))
    if(
      NOT
        OXYGEN_CONAN_PACKAGE_BUILD
      AND
        _option
          IN_LIST
          _oxygen_narrowable_options
      AND
        NOT
          ${_option}
    )
      continue()
    endif()
    message(
      FATAL_ERROR
      "${_option}=${${_option}} conflicts with the Conan configuration (${${_expected}}). "
      "Configure with the current Conan/Oxygen preset, or regenerate dependencies "
      "with matching recipe options. Only disabling "
      "provisioned optional outputs is allowed in local builds."
    )
  endif()
endforeach()
if(DEFINED OXYGEN_CONAN_MODULES)
  if(OXYGEN_CONAN_PACKAGE_BUILD)
    if(NOT OXYGEN_ENABLED_MODULES STREQUAL OXYGEN_CONAN_MODULES)
      message(
        FATAL_ERROR
        "The package module selection must match Conan's modules option."
      )
    endif()
  elseif(OXYGEN_CONAN_MODULES)
    if(OXYGEN_BUILD_FULL_ENGINE)
      message(
        FATAL_ERROR
        "The full engine requires a full Conan graph. Regenerate with modules=full."
      )
    endif()
    foreach(_module IN LISTS OXYGEN_ENABLED_MODULES)
      if(NOT _module IN_LIST OXYGEN_CONAN_MODULES)
        message(
          FATAL_ERROR
          "Module ${_module} is outside the installed Conan module selection."
        )
      endif()
    endforeach()
  endif()
endif()

# ImGui and every client must agree on this instrumentation ABI. Conan owns
# the dependency variant; changing only the CMake switch is not sufficient.
if(
  (
    OXYGEN_BUILD_UI_TESTS
    AND
      NOT
        OXYGEN_IMGUI_TEST_ENGINE_AVAILABLE
  )
  OR
    (
      OXYGEN_IMGUI_TEST_ENGINE_AVAILABLE
      AND
        NOT
          OXYGEN_BUILD_UI_TESTS
    )
)
  message(
    FATAL_ERROR
    "UI test option differs from ImGui. Reinstall Conan dependencies with -o ui_tests=True/False to match."
  )
endif()
if(OXYGEN_BUILD_UI_TESTS)
  if(NOT OXYGEN_BUILD_FULL_ENGINE OR NOT OXYGEN_BUILD_EXAMPLES)
    message(
      FATAL_ERROR
      "OXYGEN_BUILD_UI_TESTS requires the full engine and OXYGEN_BUILD_EXAMPLES"
    )
  endif()
endif()
