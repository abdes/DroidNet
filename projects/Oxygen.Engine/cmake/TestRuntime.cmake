# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RuntimeEnvironment.cmake")

# TEST_LAUNCHER is used both by GoogleTest discovery and by CTest. Keep the
# environment attached to the executable instead of guessing an ordinary Debug
# install directory or depending on the shell that happened to start the build.
function(oxygen_configure_test_runtime target)
  if(NOT WIN32)
    return()
  endif()
  get_target_property(_configured ${target} OXYGEN_TEST_RUNTIME_CONFIGURED)
  if(_configured)
    return()
  endif()
  set_property(
    TARGET
      ${target}
    PROPERTY
      OXYGEN_TEST_RUNTIME_CONFIGURED
        TRUE
  )
  oxygen_get_runtime_launcher(${target} _launcher)
  # Resolve list-valued expressions before GoogleTest serializes TEST_LAUNCHER.
  # Its serialization differs between CMake releases; the script path is always
  # one argument, while the generated script owns the runtime-directory list.
  string(SHA256 _id "${CMAKE_CURRENT_BINARY_DIR}/${target}")
  string(SUBSTRING "${_id}" 0 16 _id)
  set(_script "${CMAKE_CURRENT_BINARY_DIR}/test-runtime/${_id}-$<CONFIG>.cmake")
  file(
    GENERATE
    OUTPUT
    "${_script}"
    CONTENT
      "set(OXYGEN_RUNTIME_COMMAND [==[${_launcher}]==])\ninclude([==[${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunTest.cmake]==])\n"
  )
  set(
    _launcher
    "${CMAKE_COMMAND}"
    -P
    "${_script}"
    --
  )
  get_property(_existing TARGET ${target} PROPERTY TEST_LAUNCHER)
  list(APPEND _launcher ${_existing})
  set_property(
    TARGET
      ${target}
    PROPERTY
      TEST_LAUNCHER
        "${_launcher}"
  )
endfunction()
