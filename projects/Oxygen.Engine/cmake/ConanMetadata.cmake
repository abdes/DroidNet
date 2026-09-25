# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Conan reads evaluated usage requirements and File API artifact/dependency data.
# Keep the native targets authoritative instead of maintaining a second module
# dependency graph, Debug suffix table or set of static-library definitions.
function(oxygen_generate_conan_metadata)
  if(NOT OXYGEN_CONAN_PACKAGE_BUILD)
    return()
  endif()
  get_property(_targets GLOBAL PROPERTY OXYGEN_INSTALLED_TARGETS)
  set(_metadata "{}")
  foreach(_target IN LISTS _targets)
    get_target_property(_name "${_target}" EXPORT_NAME)
    string(
      JSON
      _metadata
      SET "${_metadata}"
      "${_target}"
      "\"${_name}\""
    )
    get_target_property(_type "${_target}" TYPE)
    if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY)$")
      file(
        GENERATE
        OUTPUT
        "${PROJECT_BINARY_DIR}/conan-metadata/$<CONFIG>/${_target}.LIBRARY_NAME"
        CONTENT "$<TARGET_LINKER_FILE_BASE_NAME:${_target}>"
      )
    endif()
    foreach(_property IN ITEMS COMPILE_DEFINITIONS COMPILE_OPTIONS)
      get_property(_value TARGET "${_target}" PROPERTY "INTERFACE_${_property}")
      # One evaluated list entry per line; all three CMake properties are lists.
      # These are build-only intermediates, never installed metadata or paths.
      file(
        GENERATE
        OUTPUT
        "${PROJECT_BINARY_DIR}/conan-metadata/$<CONFIG>/${_target}.${_property}-$<COMPILE_LANGUAGE>"
        CONTENT "$<JOIN:$<TARGET_GENEX_EVAL:${_target},${_value}>,\n>"
      )
    endforeach()
    get_property(_value TARGET "${_target}" PROPERTY INTERFACE_LINK_OPTIONS)
    file(
      GENERATE
      OUTPUT
      "${PROJECT_BINARY_DIR}/conan-metadata/$<CONFIG>/${_target}.LINK_OPTIONS"
      CONTENT "$<JOIN:$<TARGET_GENEX_EVAL:${_target},${_value}>,\n>"
    )
  endforeach()
  file(
    CONFIGURE
    OUTPUT "${PROJECT_BINARY_DIR}/conan-metadata/targets.json"
    CONTENT "${_metadata}\n"
    @ONLY
  )
endfunction()
