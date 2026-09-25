# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)

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
  get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(
    _launcher
    "${CMAKE_COMMAND}"
    -E
    env
    --modify
    "PATH=path_list_prepend:${_compiler_bin}"
  )
  if(DEFINED OXYGEN_CONAN_DEPLOY_DIR)
    if(OXYGEN_WITH_ASAN)
      set(_configuration Asan)
    else()
      set(_configuration "$<CONFIG>")
    endif()
    list(
      APPEND
      _launcher
      --modify
      "PATH=path_list_prepend:${OXYGEN_CONAN_DEPLOY_DIR}/${_configuration}/bin"
    )
  endif()
  # Expand a sequence of --modify arguments, one per transitive DLL directory.
  # The executable directory is always present, so this never produces an empty
  # launcher argument even when the executable links only static libraries.
  set(
    _dll_dirs
    "$<REMOVE_DUPLICATES:$<TARGET_FILE_DIR:${target}>;$<TARGET_RUNTIME_DLL_DIRS:${target}>>"
  )
  list(
    APPEND
    _launcher
    --modify
    "PATH=path_list_prepend:$<JOIN:${_dll_dirs},;--modify;PATH=path_list_prepend:>"
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
