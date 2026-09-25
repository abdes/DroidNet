# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)

# VERSION_FILE is relative to the caller's source directory unless absolute.
# Keep the numeric limits in sync with Oxygen's uint8_t public version API.
function(asap_version_read)
  cmake_parse_arguments(PARSE_ARGV 0 x "" "VERSION_FILE" "")
  if(x_UNPARSED_ARGUMENTS OR x_KEYWORDS_MISSING_VALUES)
    message(
      FATAL_ERROR
      "asap_version_read expects an optional VERSION_FILE path."
    )
  endif()
  if(DEFINED x_VERSION_FILE)
    if(x_VERSION_FILE STREQUAL "")
      message(FATAL_ERROR "VERSION_FILE must not be empty.")
    endif()
    set(version_file "${x_VERSION_FILE}")
  else()
    set(version_file "VERSION")
  endif()
  cmake_path(
    ABSOLUTE_PATH
    version_file
    BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    NORMALIZE
  )
  if(NOT EXISTS "${version_file}" OR IS_DIRECTORY "${version_file}")
    message(FATAL_ERROR "Version file not found: ${version_file}")
  endif()
  file(READ "${version_file}" version)
  string(STRIP "${version}" version)
  if(NOT version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(
      FATAL_ERROR
      "Invalid version '${version}' in ${version_file}; expected major.minor.patch (0..255 each)."
    )
  endif()
  set(
    _components
    "${CMAKE_MATCH_1}"
    "${CMAKE_MATCH_2}"
    "${CMAKE_MATCH_3}"
  )
  foreach(_component IN LISTS _components)
    string(LENGTH "${_component}" _length)
    if(_length GREATER 3 OR _component MATCHES "^0[0-9]")
      message(
        FATAL_ERROR
        "Invalid version '${version}'; components must be canonical decimal values in 0..255."
      )
    endif()
    if(_component GREATER 255)
      message(
        FATAL_ERROR
        "Invalid version '${version}'; each component must be in 0..255."
      )
    endif()
  endforeach()
  if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(
      DIRECTORY
      APPEND
      PROPERTY
        CMAKE_CONFIGURE_DEPENDS
          "${version_file}"
    )
  endif()
  list(GET _components 0 _major)
  list(GET _components 1 _minor)
  list(GET _components 2 _patch)
  set(META_VERSION_MAJOR "${_major}" PARENT_SCOPE)
  set(META_VERSION_MINOR "${_minor}" PARENT_SCOPE)
  set(META_VERSION_PATCH "${_patch}" PARENT_SCOPE)
  set(META_VERSION "${version}" PARENT_SCOPE)
endfunction()
