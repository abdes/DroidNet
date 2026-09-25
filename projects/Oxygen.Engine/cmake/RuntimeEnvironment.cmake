# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)

function(oxygen_get_runtime_launcher target output)
  if(NOT WIN32)
    set(${output} "${CMAKE_COMMAND};-E;env;--" PARENT_SCOPE)
    return()
  endif()
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
  # Prepend each transitive DLL directory without changing the caller's shell.
  # Including the executable directory keeps this list nonempty for static builds.
  set(
    _dll_dirs
    "$<REMOVE_DUPLICATES:$<TARGET_FILE_DIR:${target}>;$<TARGET_RUNTIME_DLL_DIRS:${target}>>"
  )
  list(
    APPEND
    _launcher
    --modify
    "PATH=path_list_prepend:$<JOIN:${_dll_dirs},;--modify;PATH=path_list_prepend:>"
  )
  list(APPEND _launcher --)
  set(${output} "${_launcher}" PARENT_SCOPE)
endfunction()
